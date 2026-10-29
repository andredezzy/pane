import stylisticPlugin from "@stylistic/eslint-plugin";
import tsParser from "@typescript-eslint/parser";
import { defineConfig } from "eslint/config";

// ESLint runs one rule, which Biome has no counterpart for through 2.6.4:
// blank lines around statements. bun run lint and the Gates workflow point
// it at tests/, the scenario runner, so it walks no build folders.
export default defineConfig([
	{
		files: ["**/*.{ts,js,mjs}"],
		languageOptions: {
			parser: tsParser,
			parserOptions: {
				ecmaVersion: "latest",
				sourceType: "module",
			},
		},
		plugins: {
			stylistic: stylisticPlugin,
		},
		rules: {
			"stylistic/padding-line-between-statements": [
				"error",
				{
					blankLine: "always",
					prev: "*",
					next: [
						"if",
						"return",
						"function",
						"interface",
						"type",
						"multiline-const",
						"multiline-let",
						"multiline-var",
						"class",
						"export",
						"try",
						"throw",
						"break",
						"continue",
						"multiline-expression",
					],
				},
				{
					blankLine: "always",
					prev: [
						"if",
						"class",
						"function",
						"interface",
						"type",
						"export",
						"try",
						"multiline-const",
						"multiline-let",
						"multiline-var",
						"multiline-expression",
					],
					next: "*",
				},
				{
					blankLine: "always",
					prev: "multiline-expression",
					next: "multiline-expression",
				},
				{ blankLine: "any", prev: "export", next: "export" },
			],
		},
	},
]);
