#include <openssl/evp.h>
#include <stdio.h>

int main(int argc, char **argv) {
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len = 0;
    unsigned char buffer[8192];
    size_t bytes_read;

    if (argc != 2) {
        fprintf(stderr, "usage: %s <file>\n", argv[0]);
        return 1;
    }

    FILE *fp = fopen(argv[1], "rb");
    if (fp == NULL) {
        perror("fopen");
        return 1;
    }

    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (ctx == NULL) {
        fprintf(stderr, "EVP_MD_CTX_new failed\n");
        fclose(fp);
        return 1;
    }

    if (EVP_DigestInit_ex(ctx, EVP_md5(), NULL) != 1) {
        fprintf(stderr, "MD5 digest failed\n");
        EVP_MD_CTX_free(ctx);
        fclose(fp);
        return 1;
    }

    while ((bytes_read = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
        if (EVP_DigestUpdate(ctx, buffer, bytes_read) != 1) {
            fprintf(stderr, "MD5 digest failed\n");
            EVP_MD_CTX_free(ctx);
            fclose(fp);
            return 1;
        }
    }

    if (ferror(fp)) {
        perror("fread");
        EVP_MD_CTX_free(ctx);
        fclose(fp);
        return 1;
    }

    if (EVP_DigestFinal_ex(ctx, digest, &digest_len) != 1) {
        fprintf(stderr, "MD5 digest failed\n");
        EVP_MD_CTX_free(ctx);
        fclose(fp);
        return 1;
    }

    EVP_MD_CTX_free(ctx);
    fclose(fp);

    for (unsigned int i = 0; i < digest_len; i++) {
        printf("%02x", digest[i]);
    }
    printf("  %s\n", argv[1]);

    return 0;
}
