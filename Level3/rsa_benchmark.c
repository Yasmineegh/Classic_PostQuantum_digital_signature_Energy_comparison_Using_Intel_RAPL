#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/rand.h>
#include <openssl/err.h>
#include <openssl/sha.h>
#include "benchmark_framework.h"

// Context specific to RSA, holding the keypair and pre-allocated contexts
typedef struct {
    EVP_PKEY *keypair;
    int key_size;
    const EVP_MD *hash_function;  // Selected hash function based on key size
    EVP_MD_CTX *sign_ctx;
    EVP_MD_CTX *verify_ctx;
} rsa_context_t;

static void print_openssl_errors() {
    ERR_print_errors_fp(stderr);
}

// Select appropriate hash function based on key size for security level matching
static const EVP_MD* get_hash_function(int key_size) {
    if (key_size == 2048 || key_size == 3072) {
        return EVP_sha256();  // 128-bit security level
    } else if (key_size == 7680) {
        return EVP_sha384();  // 192-bit security level
    } else if (key_size == 15360) {
        return EVP_sha512();  // 256-bit security level
    }
    return EVP_sha256();  // default
}

static const char* get_hash_name(const EVP_MD *md) {
    if (md == EVP_sha256()) return "SHA-256";
    if (md == EVP_sha384()) return "SHA-384";
    if (md == EVP_sha512()) return "SHA-512";
    return "Unknown";
}

// --- Implementation of the crypto_operations_t interface for RSA ---

static int rsa_init_and_generate_data(benchmark_data_t *data, const char* variant) {
    int key_size = atoi(variant);
    if (key_size != 2048 && key_size != 3072 && key_size != 7680 && key_size != 15360) {
        log_printf("ERROR: Invalid RSA key size: %s. Use 2048, 3072, 7680, or 15360.\n", variant);
        return -1;
    }

    rsa_context_t *ctx = malloc(sizeof(rsa_context_t));
    if (!ctx) return -1;
    
    memset(ctx, 0, sizeof(rsa_context_t));
    ctx->key_size = key_size;
    ctx->hash_function = get_hash_function(key_size);
    data->algo_context = ctx;

    // Pre-allocate MD contexts
    ctx->sign_ctx = EVP_MD_CTX_new();
    ctx->verify_ctx = EVP_MD_CTX_new();
    if (!ctx->sign_ctx || !ctx->verify_ctx) {
        log_printf("ERROR: Failed to allocate MD contexts\n");
        return -1;
    }

    log_printf("Generating RSA-%d keypair...\n", key_size);
    EVP_PKEY_CTX *pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, NULL);
    if (!pctx || EVP_PKEY_keygen_init(pctx) <= 0 ||
        EVP_PKEY_CTX_set_rsa_keygen_bits(pctx, key_size) <= 0 ||
        EVP_PKEY_keygen(pctx, &ctx->keypair) <= 0) {
        print_openssl_errors();
        if (pctx) EVP_PKEY_CTX_free(pctx);
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
    
    log_printf("✓ Generated RSA-%d keypair (PSS padding, %s) and %d random messages.\n", 
               key_size, get_hash_name(ctx->hash_function), MAX_MESSAGES);
    log_printf("OpenSSL version: %s\n", OpenSSL_version(OPENSSL_VERSION));
    
    return 0;
}

static int rsa_precompute_signatures(benchmark_data_t *data) {
    rsa_context_t *ctx = data->algo_context;
    log_printf("Pre-computing %d signatures for verification benchmark...\n", data->num_messages);

    for (int i = 0; i < data->num_messages; i++) {
        EVP_MD_CTX_reset(ctx->sign_ctx);
        
        EVP_PKEY_CTX *pkey_ctx;
        size_t sig_len = MAX_GENERIC_SIGNATURE_SIZE;
        
        if (EVP_DigestSignInit(ctx->sign_ctx, &pkey_ctx, ctx->hash_function, NULL, ctx->keypair) <= 0) {
            print_openssl_errors();
            return -1;
        }
        
        if (EVP_PKEY_CTX_set_rsa_padding(pkey_ctx, RSA_PKCS1_PSS_PADDING) <= 0) {
            print_openssl_errors();
            return -1;
        }
        
        if (EVP_DigestSignUpdate(ctx->sign_ctx, data->messages[i].data, data->messages[i].length) <= 0 ||
            EVP_DigestSignFinal(ctx->sign_ctx, data->signatures[i].signature, &sig_len) <= 0) {
            print_openssl_errors();
            return -1;
        }
        data->signatures[i].signature_length = sig_len;
    }
    
    log_printf("✓ Pre-computed %d signatures.\n", data->num_messages);
    return 0;
}

static int rsa_sign_op(benchmark_data_t *data, int msg_idx, unsigned char* temp_sig_buf, size_t* temp_sig_len) {
    rsa_context_t *ctx = data->algo_context;
    
    EVP_MD_CTX_reset(ctx->sign_ctx);
    
    EVP_PKEY_CTX *pkey_ctx;
    
    if (EVP_DigestSignInit(ctx->sign_ctx, &pkey_ctx, ctx->hash_function, NULL, ctx->keypair) <= 0) {
        return -1;
    }
    
    if (EVP_PKEY_CTX_set_rsa_padding(pkey_ctx, RSA_PKCS1_PSS_PADDING) <= 0) {
        return -1;
    }
    
    if (EVP_DigestSignUpdate(ctx->sign_ctx, data->messages[msg_idx].data, data->messages[msg_idx].length) <= 0 ||
        EVP_DigestSignFinal(ctx->sign_ctx, temp_sig_buf, temp_sig_len) <= 0) {
        return -1;
    }
    
    return 0;
}

static int rsa_verify_op(benchmark_data_t *data, int msg_idx) {
    rsa_context_t *ctx = data->algo_context;
    
    EVP_MD_CTX_reset(ctx->verify_ctx);

    EVP_PKEY_CTX *pkey_ctx;
    
    if (EVP_DigestVerifyInit(ctx->verify_ctx, &pkey_ctx, ctx->hash_function, NULL, ctx->keypair) <= 0) {
        return -1;
    }
    
    if (EVP_PKEY_CTX_set_rsa_padding(pkey_ctx, RSA_PKCS1_PSS_PADDING) <= 0) {
        return -1;
    }

    if (EVP_DigestVerifyUpdate(ctx->verify_ctx, data->messages[msg_idx].data, data->messages[msg_idx].length) <= 0 ||
        EVP_DigestVerifyFinal(ctx->verify_ctx, data->signatures[msg_idx].signature, data->signatures[msg_idx].signature_length) != 1) {
        return -1;
    }

    return 0;
}

static void rsa_cleanup(benchmark_data_t *data) {
    if (data && data->algo_context) {
        rsa_context_t *ctx = data->algo_context;
        
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
    const char *variant = "3072"; // Default key size
    if (argc > 1) {
        variant = argv[1];
    } else {
        printf("Usage: %s <key_size>\n", argv[0]);
        printf("Available key sizes: 2048, 3072, 7680, 15360\n");
        printf("Using default: %s\n", variant);
    }
    
    OpenSSL_add_all_algorithms();
    ERR_load_crypto_strings();

    crypto_operations_t rsa_ops = {
        .name = "RSA",
        .short_name = "rsa",
        .init_and_generate_data = rsa_init_and_generate_data,
        .precompute_signatures = rsa_precompute_signatures,
        .sign_op = rsa_sign_op,
        .verify_op = rsa_verify_op,
        .cleanup = rsa_cleanup
    };

    return run_complete_benchmark(&rsa_ops, variant);
}