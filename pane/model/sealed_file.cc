// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/sealed_file.h"

#include <utility>
#include <vector>

#include "base/base64.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/types/expected_macros.h"
#include "crypto/aead.h"
#include "crypto/random.h"

namespace pane::model {

namespace {

// AES-GCM's, as CryptoKit seals.
constexpr size_t kNonceSize = 12;

}  // namespace

SealedFile::SealedFile(const base::FilePath& directory, std::string_view name)
    : path_(directory.AppendASCII(std::string(name) + ".json")), name_(name) {}

base::expected<std::optional<base::Value>, FileError> SealedFile::Load(
    const Key& key) const {
  ASSIGN_OR_RETURN(std::optional<base::Value> disk, ReadJsonFile(path_));
  if (!disk) {
    return std::nullopt;
  }
  const std::string* box =
      disk->is_dict() ? disk->GetDict().FindString("box") : nullptr;
  const std::optional<std::vector<uint8_t>> combined =
      box ? base::Base64Decode(*box) : std::nullopt;
  if (!combined || combined->size() < kNonceSize) {
    return base::unexpected(FileError::kMalformed);
  }
  const auto [nonce, ciphertext] = base::span(*combined).split_at(kNonceSize);
  const std::optional<std::vector<uint8_t>> json =
      crypto::aead::Open(crypto::aead::AES_256_GCM, key, ciphertext, nonce,
                         base::as_byte_span(name_));
  if (!json) {
    return base::unexpected(FileError::kRefused);
  }
  std::optional<base::Value> state =
      base::JSONReader::Read(base::as_string_view(*json), base::JSON_PARSE_RFC);
  if (!state) {
    return base::unexpected(FileError::kMalformed);
  }
  return std::move(state);
}

bool SealedFile::Save(const base::Value& state, const Key& key) const {
  const std::optional<std::string> json = base::WriteJson(state);
  if (!json) {
    return false;
  }
  const auto nonce = crypto::RandBytesAsArray<kNonceSize>();
  std::vector<uint8_t> combined(nonce.begin(), nonce.end());
  const std::vector<uint8_t> sealed = crypto::aead::Seal(
      crypto::aead::AES_256_GCM, key, base::as_byte_span(*json), nonce,
      base::as_byte_span(name_));
  combined.insert(combined.end(), sealed.begin(), sealed.end());
  return WriteJsonFile(path_, base::Value(base::DictValue().Set(
                                  "box", base::Base64Encode(combined))));
}

}  // namespace pane::model
