// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/browser/fingerprints.h"

#include <optional>
#include <utility>

#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/values.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/spare_render_process_host_manager.h"
#include "content/public/common/result_codes.h"
#include "content/public/common/content_switches.h"
#include "pane/browser/engine_trace.h"

namespace pane {

namespace {

constexpr char kFingerprintSwitch[] = "pane-fingerprint";

std::optional<blink::UserAgentBrandList> ParseBrands(
    const base::ListValue* list) {
  if (!list) {
    return std::nullopt;
  }
  blink::UserAgentBrandList brands;
  for (const base::Value& entry : *list) {
    const base::DictValue* dict = entry.GetIfDict();
    const std::string* brand = dict ? dict->FindString("brand") : nullptr;
    const std::string* version = dict ? dict->FindString("version") : nullptr;
    if (!brand || !version) {
      return std::nullopt;
    }
    brands.emplace_back(*brand, *version);
  }
  return brands;
}

std::optional<blink::UserAgentMetadata> ParseMetadata(
    const base::DictValue* dict) {
  if (!dict) {
    return std::nullopt;
  }
  std::optional<blink::UserAgentBrandList> brands =
      ParseBrands(dict->FindList("brands"));
  std::optional<blink::UserAgentBrandList> full_versions =
      ParseBrands(dict->FindList("fullVersionList"));
  const std::string* full_version = dict->FindString("fullVersion");
  const std::string* platform = dict->FindString("platform");
  const std::string* platform_version = dict->FindString("platformVersion");
  const std::string* architecture = dict->FindString("architecture");
  const std::string* bitness = dict->FindString("bitness");
  const std::string* model = dict->FindString("model");
  const base::ListValue* form_factors = dict->FindList("formFactors");
  if (!brands || !full_versions || !full_version || !platform ||
      !platform_version || !architecture || !bitness || !model ||
      !form_factors) {
    return std::nullopt;
  }

  blink::UserAgentMetadata metadata;
  metadata.brand_version_list = std::move(*brands);
  metadata.brand_full_version_list = std::move(*full_versions);
  metadata.full_version = *full_version;
  metadata.platform = *platform;
  metadata.platform_version = *platform_version;
  metadata.architecture = *architecture;
  metadata.bitness = *bitness;
  metadata.model = *model;
  metadata.mobile = dict->FindBool("mobile").value_or(false);
  metadata.wow64 = dict->FindBool("wow64").value_or(false);
  for (const base::Value& form_factor : *form_factors) {
    if (form_factor.is_string() &&
        blink::UserAgentMetadata::IsValidFormFactor(form_factor.GetString())) {
      metadata.form_factors.push_back(form_factor.GetString());
    }
  }
  return metadata;
}

// The profile identifier of `browser_context`: its directory's name.
std::string IdentifierOf(content::BrowserContext* browser_context) {
  return browser_context->GetPath().BaseName().AsUTF8Unsafe();
}

// Shuts down the profile's spare renderers: they started with its previous
// fingerprint, and the next page would take one.
void DropSpares(const std::string& identifier) {
  std::vector<content::RenderProcessHost*> spares;
  for (content::RenderProcessHost* spare :
       content::SpareRenderProcessHostManager::Get().GetSpares()) {
    if (IdentifierOf(spare->GetBrowserContext()) == identifier) {
      spares.push_back(spare);
    }
  }
  for (content::RenderProcessHost* spare : spares) {
    spare->Shutdown(content::RESULT_CODE_NORMAL_EXIT);
  }
}

}  // namespace

Fingerprint::Fingerprint() = default;
Fingerprint::Fingerprint(const Fingerprint&) = default;
Fingerprint& Fingerprint::operator=(const Fingerprint&) = default;
Fingerprint::~Fingerprint() = default;

// static
Fingerprints& Fingerprints::Get() {
  static base::NoDestructor<Fingerprints> fingerprints;
  return *fingerprints;
}

Fingerprints::Fingerprints() = default;
Fingerprints::~Fingerprints() = default;

bool Fingerprints::Set(const std::string& identifier,
                       std::string_view json,
                       std::string* error) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  if (json.empty()) {
    fingerprints_.erase(identifier);
    DropSpares(identifier);
    PANE_TRACE(
        "engine", "Fingerprints.set",
        base::DictValue().Set("identifier", identifier).Set("set", false));
    return true;
  }

  std::optional<base::DictValue> root =
      base::JSONReader::ReadDict(json, base::JSON_PARSE_RFC);
  const std::string* user_agent = root ? root->FindString("userAgent") : nullptr;
  std::optional<blink::UserAgentMetadata> metadata =
      root ? ParseMetadata(root->FindDict("userAgentMetadata")) : std::nullopt;
  const base::ListValue* languages = root ? root->FindList("languages") : nullptr;
  const base::DictValue* renderer = root ? root->FindDict("renderer") : nullptr;
  if (!user_agent || user_agent->empty() || !metadata || !languages ||
      !renderer) {
    *error = "Not a fingerprint: it needs userAgent, userAgentMetadata, "
             "languages, and renderer.";
    return false;
  }

  Fingerprint fingerprint;
  fingerprint.user_agent = *user_agent;
  fingerprint.user_agent_metadata = std::move(*metadata);
  for (const base::Value& language : *languages) {
    if (language.is_string() && !language.GetString().empty()) {
      fingerprint.languages.push_back(language.GetString());
    }
  }
  fingerprint.renderer = base::WriteJson(*renderer).value_or("{}");
  fingerprints_[identifier] = std::move(fingerprint);
  DropSpares(identifier);

  PANE_TRACE("engine", "Fingerprints.set",
             base::DictValue()
                 .Set("identifier", identifier)
                 .Set("set", true)
                 .Set("userAgent", *user_agent));
  return true;
}

const Fingerprint* Fingerprints::For(
    content::BrowserContext* browser_context) const {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  if (!browser_context) {
    return nullptr;
  }
  auto found = fingerprints_.find(IdentifierOf(browser_context));
  return found == fingerprints_.end() ? nullptr : &found->second;
}

void Fingerprints::AppendSwitches(base::CommandLine* command_line,
                                  int child_process_id) const {
  if (command_line->GetSwitchValueASCII(switches::kProcessType) !=
      switches::kRendererProcess) {
    return;
  }

  content::RenderProcessHost* process =
      content::RenderProcessHost::FromID(child_process_id);
  const Fingerprint* fingerprint =
      process ? For(process->GetBrowserContext()) : nullptr;
  if (!fingerprint) {
    return;
  }

  command_line->AppendSwitchASCII(kFingerprintSwitch, fingerprint->renderer);
  PANE_TRACE("engine", "Fingerprints.renderer",
             base::DictValue()
                 .Set("identifier", IdentifierOf(process->GetBrowserContext()))
                 .Set("process", child_process_id));
}

}  // namespace pane
