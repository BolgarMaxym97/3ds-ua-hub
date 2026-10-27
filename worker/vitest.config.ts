import { cloudflareTest, readD1Migrations } from "@cloudflare/vitest-plugin";
import { defineConfig } from "vitest/config";

export default defineConfig(async () => {
  const migrations = await readD1Migrations("./migrations");
  // A throwaway key pair: the Worker signs with the private half, the tests verify with the public one.
  const keys = (await crypto.subtle.generateKey({ name: "Ed25519" }, true, ["sign", "verify"])) as CryptoKeyPair;
  return {
    plugins: [
      cloudflareTest({
        wrangler: { configPath: "./wrangler.jsonc" },
        miniflare: {
          bindings: {
            TEST_MIGRATIONS: migrations,
            SIGNING_KEY: JSON.stringify(await crypto.subtle.exportKey("jwk", keys.privateKey)),
            TEST_PUBLIC_KEY: JSON.stringify(await crypto.subtle.exportKey("jwk", keys.publicKey)),
          },
        },
      }),
    ],
    test: { setupFiles: ["./test/apply-migrations.ts"] },
  };
});
