#include <oqs/oqs.h>
#include "benchmark_framework.h"

// Context specific to an OQS algorithm
typedef struct {
    OQS_SIG *sig;
    uint8_t *public_key;
    uint8_t *secret_key;
} oqs_context_t;

// --- Generic OQS Implementation of the crypto_operations_t interface ---

static int oqs_init_and_generate_data(benchmark_data_t *data, const char* variant) {
    oqs_context_t *ctx = malloc(sizeof(oqs_context_t));
    if (!ctx) return -1;
    ctx->public_key = NULL;
    ctx->secret_key = NULL;
    data->algo_context = ctx;

    ctx->sig = OQS_SIG_new(variant);
    if (ctx->sig == NULL) {
        log_printf("ERROR: OQS_SIG_new failed for %s\n", variant);
        return -1;
    }

    log_printf("Generating keypair for %s...\n", variant);
    ctx->public_key = malloc(ctx->sig->length_public_key);
    ctx->secret_key = malloc(ctx->sig->length_secret_key);
    if (!ctx->public_key || !ctx->secret_key) {
        log_printf("ERROR: Failed to allocate memory for OQS keys.\n");
        return -1;
    }

    if (OQS_SIG_keypair(ctx->sig, ctx->public_key, ctx->secret_key) != OQS_SUCCESS) {
        log_printf("ERROR: OQS_SIG_keypair failed for %s.\n", variant);
        return -1;
    }

    for (int i = 0; i < MAX_MESSAGES; i++) {
        OQS_randombytes(data->messages[i].data, MESSAGE_SIZE);
        data->messages[i].length = MESSAGE_SIZE;
    }
    data->num_messages = MAX_MESSAGES;
    log_printf("✓ Generated %s keypair (SHAKE256) and %d random messages.\n", variant, MAX_MESSAGES);
    return 0;
}

static int oqs_precompute_signatures(benchmark_data_t *data) {
    oqs_context_t *ctx = data->algo_context;
    log_printf("Pre-computing %d signatures for verification benchmark...\n", data->num_messages);

    for (int i = 0; i < data->num_messages; i++) {
        size_t sig_len = MAX_GENERIC_SIGNATURE_SIZE;
        if (OQS_SIG_sign(ctx->sig, data->signatures[i].signature, &sig_len,
                         data->messages[i].data, data->messages[i].length,
                         ctx->secret_key) != OQS_SUCCESS) {
            log_printf("ERROR: OQS_SIG_sign failed during pre-computation for message %d.\n", i);
            return -1;
        }
        data->signatures[i].signature_length = sig_len;
    }
    log_printf("✓ Pre-computed %d signatures.\n", data->num_messages);
    return 0;
}

static int oqs_sign_op(benchmark_data_t *data, int msg_idx, unsigned char* temp_sig_buf, size_t* temp_sig_len) {
    oqs_context_t *ctx = data->algo_context;
    if (OQS_SIG_sign(ctx->sig, temp_sig_buf, temp_sig_len,
                     data->messages[msg_idx].data, data->messages[msg_idx].length,
                     ctx->secret_key) != OQS_SUCCESS) {
        return -1;
    }
    return 0;
}

static int oqs_verify_op(benchmark_data_t *data, int msg_idx) {
    oqs_context_t *ctx = data->algo_context;
    if (OQS_SIG_verify(ctx->sig, data->messages[msg_idx].data, data->messages[msg_idx].length,
                       data->signatures[msg_idx].signature, data->signatures[msg_idx].signature_length,
                       ctx->public_key) != OQS_SUCCESS) {
        return -1;
    }
    return 0;
}

static void oqs_cleanup(benchmark_data_t *data) {
    if (data && data->algo_context) {
        oqs_context_t *ctx = data->algo_context;
        if (ctx->public_key) free(ctx->public_key);
        if (ctx->secret_key) free(ctx->secret_key);
        if (ctx->sig) OQS_SIG_free(ctx->sig);
        free(ctx);
        data->algo_context = NULL;
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        log_printf("Usage: %s <Dilithium_Variant>\n", argv[0]);
        log_printf("Available variants: Dilithium2, Dilithium3, Dilithium5\n");
        return 1;
    }
    const char *variant = argv[1];

    if (strcmp(variant, "Dilithium2") != 0 && strcmp(variant, "Dilithium3") != 0 && strcmp(variant, "Dilithium5") != 0) {
        log_printf("ERROR: Unknown Dilithium variant: %s\n", variant);
        return 1;
    }
    
    // Map OQS internal names
    const char* oqs_variant_name;
    if (strcmp(variant, "Dilithium2") == 0) oqs_variant_name = OQS_SIG_alg_dilithium_2;
    if (strcmp(variant, "Dilithium3") == 0) oqs_variant_name = OQS_SIG_alg_dilithium_3;
    if (strcmp(variant, "Dilithium5") == 0) oqs_variant_name = OQS_SIG_alg_dilithium_5;

    OQS_init();

    crypto_operations_t dilithium_ops = {
        .name = variant,
        .short_name = "dilithium",
        .init_and_generate_data = oqs_init_and_generate_data,
        .precompute_signatures = oqs_precompute_signatures,
        .sign_op = oqs_sign_op,
        .verify_op = oqs_verify_op,
        .cleanup = oqs_cleanup
    };

    int result = run_complete_benchmark(&dilithium_ops, oqs_variant_name);
    OQS_destroy();
    return result;
}