// Makes the Ed25519 key pair that signs the catalog.
// The private key goes to a file (never to the screen, so it cannot end up pasted somewhere):
//   npx wrangler secret put SIGNING_KEY < .signing-key.json
// The public key is printed: it goes into the app (app/source/catalog_key.h) and is no secret.
// Run once; a new key needs a new app release.
import { generateKeyPairSync } from "node:crypto";
import { existsSync, writeFileSync } from "node:fs";

const FILE = ".signing-key.json";
if (existsSync(FILE) && !process.argv.includes("--force")) {
  console.error(`${FILE} already exists. Delete it or pass --force to make a new key.`);
  process.exit(1);
}

const { privateKey, publicKey } = generateKeyPairSync("ed25519");
writeFileSync(FILE, JSON.stringify(privateKey.export({ format: "jwk" })), { mode: 0o600 });
const raw = Buffer.from(publicKey.export({ format: "jwk" }).x, "base64url");

console.log(`Private key written to worker/${FILE} (git ignores it). Next:`);
console.log(`  npx wrangler secret put SIGNING_KEY < ${FILE}`);
console.log("  then keep the file in a password manager and delete it from disk.\n");
console.log("Public key for the app (hex, not secret):");
console.log(raw.toString("hex"));
