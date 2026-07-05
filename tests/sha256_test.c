
#include <stdio.h>
#include <openssl/sha.h>

// Compile:
// gcc sha256_file.c -o sha256_file -lcrypto

// Run:
// ./sha256_file path/to/file
int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <file>\n", argv[0]);
        return 1;
    }

    FILE *fp = fopen(argv[1], "rb");
    if (!fp) {
        perror("fopen");
        return 1;
    }

    SHA256_CTX ctx;
    unsigned char buffer[8192];
    unsigned char hash[SHA256_DIGEST_LENGTH];

    SHA256_Init(&ctx);

    size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
        SHA256_Update(&ctx, buffer, n);
    }

    if (ferror(fp)) {
        perror("fread");
        fclose(fp);
        return 1;
    }

    SHA256_Final(hash, &ctx);
    fclose(fp);

    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        printf("%02x", hash[i]);
    }

    printf("  %s\n", argv[1]);
    return 0;
}

