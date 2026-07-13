import { readFile } from "node:fs/promises";
import path from "node:path";

const packageDir = new URL("./", import.meta.url);
const packageJson = JSON.parse(await readFile(new URL("package.json", packageDir), "utf8"));

function assertRelativeFile(specifier, label) {
  if (typeof specifier !== "string" || !specifier.startsWith("./")) {
    throw new Error(`${label} must be a relative file export`);
  }
  return new URL(specifier, packageDir);
}

async function assertReadable(url, label) {
  try {
    await readFile(url);
  } catch (err) {
    throw new Error(`${label} is not readable: ${path.basename(url.pathname)}: ${err.message}`);
  }
}

function declarationUrlForImport(fromUrl, specifier) {
  if (!specifier.startsWith(".")) return null;
  const target = new URL(specifier, fromUrl);
  if (target.pathname.endsWith(".js")) {
    return new URL(`${target.href.slice(0, -".js".length)}.d.ts`);
  }
  if (target.pathname.endsWith(".d.ts")) return target;
  return new URL(`${target.href}.d.ts`);
}

async function assertDeclarationImports(typesUrl, label, seen = new Set()) {
  const key = typesUrl.href;
  if (seen.has(key)) return;
  seen.add(key);
  const text = await readFile(typesUrl, "utf8");
  const importRe = /\b(?:import|export)\s+(?:type\s+)?(?:[^'"]*?\s+from\s+)?["']([^"']+)["']/g;
  for (const match of text.matchAll(importRe)) {
    const depUrl = declarationUrlForImport(typesUrl, match[1]);
    if (!depUrl) continue;
    await assertReadable(depUrl, `${label} dependency ${match[1]}`);
    await assertDeclarationImports(depUrl, `${label} dependency ${match[1]}`, seen);
  }
}

if (packageJson.type !== "module") {
  throw new Error("tools/wasm_js_api package must be ESM");
}
const packageTypesUrl = assertRelativeFile(packageJson.types, "package types");
await assertReadable(packageTypesUrl, "package types");
await assertDeclarationImports(packageTypesUrl, "package types");

const exportsMap = packageJson.exports;
if (!exportsMap || typeof exportsMap !== "object" || Array.isArray(exportsMap)) {
  throw new Error("package exports must be an object");
}

const expectedExports = {
  ".": ["createToolchain", "default"],
  "./agc-toolchain.js": ["createToolchain", "default"],
  "./agc-wasm.js": ["createCompiler", "default"],
  "./agc-runtime-imports.js": [
    "AGC_RUNTIME_IMPORT_MANIFEST",
    "createAgcRuntimeImports",
    "createAgcRuntimeMathEnvImports",
    "createAgcRuntimeStdioEnvImports",
  ],
  "./agc-include-inline.js": ["inlineStandardIncludes", "default"],
};

for (const [name, entry] of Object.entries(exportsMap)) {
  if (!entry || typeof entry !== "object" || Array.isArray(entry)) {
    throw new Error(`export ${name} must be an object`);
  }
  const typesUrl = assertRelativeFile(entry.types, `export ${name} types`);
  const importUrl = assertRelativeFile(entry.import, `export ${name} import`);
  await assertReadable(typesUrl, `export ${name} types`);
  await assertDeclarationImports(typesUrl, `export ${name} types`);
  await assertReadable(importUrl, `export ${name} import`);

  const mod = await import(importUrl.href);
  for (const exportName of expectedExports[name] || []) {
    if (!(exportName in mod)) {
      throw new Error(`export ${name} does not provide ${exportName}`);
    }
  }
  if (name === "./agc-runtime-imports.js") {
    const mathEnv = mod.createAgcRuntimeMathEnvImports();
    const stdioEnv = mod.createAgcRuntimeStdioEnvImports();
    const manifest = mod.AGC_RUNTIME_IMPORT_MANIFEST;
    if (!manifest || manifest.version !== 1 || !manifest.namespaces?.env) {
      throw new Error("runtime import manifest has an invalid shape");
    }
    const assertManifestGroup = (groupName, implementation) => {
      const declared = manifest.namespaces.env[groupName];
      if (!Array.isArray(declared)) {
        throw new Error(`runtime import manifest missing env.${groupName}`);
      }
      const unique = new Set(declared);
      if (unique.size !== declared.length) {
        throw new Error(`runtime import manifest env.${groupName} contains duplicates`);
      }
      const actual = Object.keys(implementation).sort();
      const expected = [...declared].sort();
      if (actual.join("\n") !== expected.join("\n")) {
        const missing = expected.filter((entry) => !(entry in implementation));
        const undeclared = actual.filter((entry) => !unique.has(entry));
        throw new Error(
          `runtime import manifest mismatch for env.${groupName}; ` +
          `missing implementations: ${missing.join(", ") || "none"}; ` +
          `undeclared imports: ${undeclared.join(", ") || "none"}`,
        );
      }
      for (const importName of expected) {
        if (typeof implementation[importName] !== "function") {
          throw new Error(`runtime import ${importName} is not a function`);
        }
      }
    };
    assertManifestGroup("math", mathEnv);
    assertManifestGroup("stdio", stdioEnv);
    const overlap = manifest.namespaces.env.math.filter(
      (entry) => manifest.namespaces.env.stdio.includes(entry),
    );
    if (overlap.length) {
      throw new Error(`runtime import manifest groups overlap: ${overlap.join(", ")}`);
    }
    for (const importName of [
      "sin", "sinf", "sinl",
      "sqrt", "sqrtf", "sqrtl",
      "pow", "powf", "powl",
      "__agc_runtime_math_sin",
      "__agc_runtime_math_sqrt",
      "__agc_runtime_math_pow",
    ]) {
      if (typeof mathEnv[importName] !== "function") {
        throw new Error(`math env imports missing ${importName}`);
      }
    }
    if (mathEnv.__agc_runtime_math_sqrt(4) !== 2) {
      throw new Error("math env runtime alias returned the wrong value");
    }
  }
}

console.log("ag_c wasm JS package exports smoke: ok");
