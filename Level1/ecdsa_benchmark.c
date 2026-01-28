#include <openssl/ec.h>
#include <openssl/obj_mac.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/err.h>
#include <openssl/sha.h>
#include "benchmark_framework.h"

// Context specific to ECDSA
typedef struct {
    EVP_PKEY *keypair;
    int curve_nid;
    const EVP_MD *hash_function;  // Selected hash function based on curve
    EVP_MD_CTX *sign_ctx;
    EVP_MD_CTX *verify_ctx;
} ecdsa_context_t;

static void print_openssl_errors() {
    ERR_print_errors_fp(stderr);
}

// Select appropriate hash function based on curve for security level matching
static const EVP_MD* get_hash_function(int curve_nid) {
    switch(curve_nid) {
        case NID_X9_62_prime256v1:  // P-256, ~128-bit security
            return EVP_sha256();
        case NID_secp384r1:          // P-384, ~192-bit security
            return EVP_sha384();
        case NID_secp521r1:          // P-521, ~256-bit security
            return EVP_sha512();
        default:
            return EVP_sha256();
    }
}

static const char* get_hash_name(const EVP_MD *md) {
    if (md == EVP_sha256()) return "SHA-256";
    if (md == EVP_sha384()) return "SHA-384";
    if (md == EVP_sha512()) return "SHA-512";
    return "Unknown";
}

static int ecdsa_init_and_generate_data(benchmark_data_t *data, const char* variant) {
    int nid;
    if (strcmp(variant, "P-256") == 0) nid = NID_X9_62_prime256v1;
    else if (strcmp(variant, "P-384") == 0) nid = NID_secp384r1;
    else if (strcmp(variant, "P-521") == 0) nid = NID_secp521r1;
    else {
        log_printf("ERROR: Invalid curve: %s. Use 'P-256', 'P-384', or 'P-521'.\n", variant);
        return -1;
    }
    
    ecdsa_context_t *ctx = malloc(sizeof(ecdsa_context_t));
    if (!ctx) return -1;
    
    memset(ctx, 0, sizeof(ecdsa_context_t));
    ctx->curve_nid = nid;
    ctx->hash_function = get_hash_function(nid);
    data->algo_context = ctx;

    // Pre-allocate MD contexts
    ctx->sign_ctx = EVP_MD_CTX_new();
    ctx->verify_ctx = EVP_MD_CTX_new();
    if (!ctx->sign_ctx || !ctx->verify_ctx) {
        log_printf("ERROR: Failed to allocate MD contexts\n");
        return -1;
    }

    log_printf("Generating ECDSA keypair for curve %s...\n", variant);
    
    EVP_PKEY_CTX *pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_EC, NULL);
    if (!pctx) { print_openssl_errors(); return -1; }

    if (EVP_PKEY_paramgen_init(pctx) <= 0) { print_openssl_errors(); EVP_PKEY_CTX_free(pctx); return -1; }
    if (EVP_PKEY_CTX_set_ec_paramgen_curve_nid(pctx, nid) <= 0) { print_openssl_errors(); EVP_PKEY_CTX_free(pctx); return -1; }
    
    EVP_PKEY *params = NULL;
    if (EVP_PKEY_paramgen(pctx, &params) <= 0) { print_openssl_errors(); EVP_PKEY_CTX_free(pctx); return -1; }
    EVP_PKEY_CTX_free(pctx);

    EVP_PKEY_CTX *kctx = EVP_PKEY_CTX_new(params, NULL);
    EVP_PKEY_free(params);
    if (!kctx) { print_openssl_errors(); return -1; }
    
    if (EVP_PKEY_keygen_init(kctx) <= 0 || EVP_PKEY_keygen(kctx, &ctx->keypair) <= 0) {
        print_openssl_errors();
        EVP_PKEY_CTX_free(kctx);
        return -1;
    }
    EVP_PKEY_CTX_free(kctx);

    // Generate test messages
    for (int i = 0; i < MAX_MESSAGES; i++) {
        if (RAND_bytes(data->messages[i].data, MESSAGE_SIZE) != 1) { 
            print_openssl_errors(); 
            return -1; 
        }
        data->messages[i].length = MESSAGE_SIZE;
    }
    data->num_messages = MAX_MESSAGES;
    
    log_printf("✓ Generated ECDSA keypair (curve: %s, %s) and %d random messages.\n", 
               variant, get_hash_name(ctx->hash_function), MAX_MESSAGES);
    log_printf("OpenSSL version: %s\n", OpenSSL_version(OPENSSL_VERSION));
    
    return 0;
}

static int ecdsa_precompute_signatures(benchmark_data_t *data) {
    ecdsa_context_t *ctx = data->algo_context;
    log_printf("Pre-computing %d signatures for verification benchmark...\n", data->num_messages);

    for (int i = 0; i < data->num_messages; i++) {
        EVP_MD_CTX_reset(ctx->sign_ctx);
        
        size_t sig_len = MAX_GENERIC_SIGNATURE_SIZE;
        if (EVP_DigestSignInit(ctx->sign_ctx, NULL, ctx->hash_function, NULL, ctx->keypair) <= 0 ||
            EVP_DigestSignUpdate(ctx->sign_ctx, data->messages[i].data, data->messages[i].length) <= 0 ||
            EVP_DigestSignFinal(ctx->sign_ctx, data->signatures[i].signature, &sig_len) <= 0) {
            print_openssl_errors();
            return -1;
        }
        data->signatures[i].signature_length = sig_len;
    }
    
    log_printf("✓ Pre-computed %d signatures.\n", data->num_messages);
    return 0;
}

static int ecdsa_sign_op(benchmark_data_t *data, int msg_idx, unsigned char* temp_sig_buf, size_t* temp_sig_len) {
    ecdsa_context_t *ctx = data->algo_context;
    
    EVP_MD_CTX_reset(ctx->sign_ctx);
    
    if (EVP_DigestSignInit(ctx->sign_ctx, NULL, ctx->hash_function, NULL, ctx->keypair) <= 0 ||
        EVP_DigestSignUpdate(ctx->sign_ctx, data->messages[msg_idx].data, data->messages[msg_idx].length) <= 0 ||
        EVP_DigestSignFinal(ctx->sign_ctx, temp_sig_buf, temp_sig_len) <= 0) {
        return -1;
    }
    
    return 0;
}

static int ecdsa_verify_op(benchmark_data_t *data, int msg_idx) {
    ecdsa_context_t *ctx = data->algo_context;
    
    EVP_MD_CTX_reset(ctx->verify_ctx);

    if (EVP_DigestVerifyInit(ctx->verify_ctx, NULL, ctx->hash_function, NULL, ctx->keypair) <= 0 ||
        EVP_DigestVerifyUpdate(ctx->verify_ctx, data->messages[msg_idx].data, data->messages[msg_idx].length) <= 0 ||
        EVP_DigestVerifyFinal(ctx->verify_ctx, data->signatures[msg_idx].signature, data->signatures[msg_idx].signature_length) != 1) {
        return -1;
    }

    return 0;
}

static void ecdsa_cleanup(benchmark_data_t *data) {
    if (data && data->algo_context) {
        ecdsa_context_t *ctx = data->algo_context;
        
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
    const char *variant = "P-256"; // Default curve
    if (argc > 1) {
        variant = argv[1];
    } else {
        printf("Usage: %s <curve_name>\n", argv[0]);
        printf("Available curves: P-256, P-384, P-521\n");
        printf("Using default: %s\n", variant);
    }

    OpenSSL_add_all_algorithms();
    ERR_load_crypto_strings();

    crypto_operations_t ecdsa_ops = {
        .name = "ECDSA",
        .short_name = "ecdsa",
        .init_and_generate_data = ecdsa_init_and_generate_data,
        .precompute_signatures = ecdsa_precompute_signatures,
        .sign_op = ecdsa_sign_op,
        .verify_op = ecdsa_verify_op,
        .cleanup = ecdsa_cleanup
    };

    return run_complete_benchmark(&ecdsa_ops, variant);
}