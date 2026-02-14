#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/err.h>
#include "benchmark_framework.h"

// Context specific to EdDSA (Ed25519)
typedef struct {
    EVP_PKEY *keypair;
    EVP_MD_CTX *sign_ctx;    // Pre-allocated signing context
    EVP_MD_CTX *verify_ctx;  // Pre-allocated verification context
} eddsa_context_t;

static void print_openssl_errors() {
    ERR_print_errors_fp(stderr);
}

// --- Implementation of the crypto_operations_t interface for EdDSA ---

static int eddsa_init_and_generate_data(benchmark_data_t *data, const char* variant) {
    // Only Ed25519 is supported for now
    if (strcmp(variant, "Ed25519") != 0) {
        log_printf("ERROR: Invalid EdDSA variant: %s. Use 'Ed25519'.\n", variant);
        return -1;
    }

    eddsa_context_t *ctx = malloc(sizeof(eddsa_context_t));
    if (!ctx) return -1;
    
    memset(ctx, 0, sizeof(eddsa_context_t));
    data->algo_context = ctx;

    // Pre-allocate MD contexts
    ctx->sign_ctx = EVP_MD_CTX_new();
    ctx->verify_ctx = EVP_MD_CTX_new();
    if (!ctx->sign_ctx || !ctx->verify_ctx) {
        log_printf("ERROR: Failed to allocate MD contexts\n");
        return -1;
    }

    log_printf("Generating Ed25519 keypair...\n");
    
    // Generate Ed25519 key
    EVP_PKEY_CTX *pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, NULL);
    if (!pctx) {
        print_openssl_errors();
        return -1;
    }

    if (EVP_PKEY_keygen_init(pctx) <= 0 || EVP_PKEY_keygen(pctx, &ctx->keypair) <= 0) {
        print_openssl_errors();
        EVP_PKEY_CTX_free(pctx);
        return -1;
    }
    EVP_PKEY_CTX_free(pctx);

    // Generate test messages
    for (int i = 0; i < MAX_MESSAGES; i++) {
        if (RAND_bytes(data->messages[i].data, MESSAGE_SIZE) != 1) {
            print_openssl_errors();
            return -1;
        }
        data->messages[i].length = MESSAGE_SIZE;
    }
    data->num_messages = MAX_MESSAGES;
    
    log_printf("✓ Generated Ed25519 keypair (SHA-512 built-in) and %d random messages.\n", MAX_MESSAGES);
    log_printf("OpenSSL version: %s\n", OpenSSL_version(OPENSSL_VERSION));
    
    return 0;
}

static int eddsa_precompute_signatures(benchmark_data_t *data) {
    eddsa_context_t *ctx = data->algo_context;
    log_printf("Pre-computing %d signatures for verification benchmark...\n", data->num_messages);

    for (int i = 0; i < data->num_messages; i++) {
        // Reset context for each signature
        EVP_MD_CTX_reset(ctx->sign_ctx);
        
        size_t sig_len = MAX_GENERIC_SIGNATURE_SIZE;
        
        // Ed25519 signing - no separate hash initialization needed
        if (EVP_DigestSignInit(ctx->sign_ctx, NULL, NULL, NULL, ctx->keypair) <= 0 ||
            EVP_DigestSign(ctx->sign_ctx, data->signatures[i].signature, &sig_len,
                          data->messages[i].data, data->messages[i].length) <= 0) {
            print_openssl_errors();
            return -1;
        }
        data->signatures[i].signature_length = sig_len;
    }
    
    log_printf("✓ Pre-computed %d signatures.\n", data->num_messages);
    return 0;
}

static int eddsa_sign_op(benchmark_data_t *data, int msg_idx, unsigned char* temp_sig_buf, size_t* temp_sig_len) {
    eddsa_context_t *ctx = data->algo_context;
    
    // Reset the pre-allocated context
    EVP_MD_CTX_reset(ctx->sign_ctx);
    
    // Perform Ed25519 signing (hashing is built-in)
    if (EVP_DigestSignInit(ctx->sign_ctx, NULL, NULL, NULL, ctx->keypair) <= 0 ||
        EVP_DigestSign(ctx->sign_ctx, temp_sig_buf, temp_sig_len,
                      data->messages[msg_idx].data, data->messages[msg_idx].length) <= 0) {
        return -1;
    }
    
    return 0;
}

static int eddsa_verify_op(benchmark_data_t *data, int msg_idx) {
    eddsa_context_t *ctx = data->algo_context;
    
    // Reset the pre-allocated context
    EVP_MD_CTX_reset(ctx->verify_ctx);

    // Perform Ed25519 verification (hashing is built-in)
    if (EVP_DigestVerifyInit(ctx->verify_ctx, NULL, NULL, NULL, ctx->keypair) <= 0 ||
        EVP_DigestVerify(ctx->verify_ctx, data->signatures[msg_idx].signature,
                        data->signatures[msg_idx].signature_length,
                        data->messages[msg_idx].data, data->messages[msg_idx].length) != 1) {
        return -1;
    }

    return 0;
}

static void eddsa_cleanup(benchmark_data_t *data) {
    if (data && data->algo_context) {
        eddsa_context_t *ctx = data->algo_context;
        
        if (ctx->keypair) {
            EVP_PKEY_free(ctx->keypair);
        }
        if (ctx->sign_ctx) {
            EVP_MD_CTX_free(ctx->sign_ctx);
        }
        if (ctx->verify_ctx) {
            EVP_MD_CTX_free(ctx->verify_ctx);
        }
        
        free(ctx);
        data->algo_context = NULL;
    }
    EVP_cleanup();
    ERR_free_strings();
}

int main(int argc, char *argv[]) {
    const char *variant = "Ed25519"; // Only variant supported
    if (argc > 1) {
        variant = argv[1];
        if (strcmp(variant, "Ed25519") != 0) {
            printf("ERROR: Only Ed25519 is supported.\n");
            printf("Usage: %s [Ed25519]\n", argv[0]);
            return 1;
        }
    } else {
        printf("Usage: %s <variant>\n", argv[0]);
        printf("Using default: %s\n", variant);
    }
    
    OpenSSL_add_all_algorithms();
    ERR_load_crypto_strings();

    crypto_operations_t eddsa_ops = {
        .name = "EdDSA",
        .short_name = "eddsa",
        .init_and_generate_data = eddsa_init_and_generate_data,
        .precompute_signatures = eddsa_precompute_signatures,
        .sign_op = eddsa_sign_op,
        .verify_op = eddsa_verify_op,
        .cleanup = eddsa_cleanup
    };

    return run_complete_benchmark(&eddsa_ops, variant);
}