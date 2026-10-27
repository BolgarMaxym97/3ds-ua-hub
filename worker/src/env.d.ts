// Secrets are not in wrangler.jsonc, so `wrangler types` does not know them.
declare global {
  interface Env {
    SIGNING_KEY: string; // Ed25519 private key as a JWK, from `npm run keygen`
  }
}

export {};
