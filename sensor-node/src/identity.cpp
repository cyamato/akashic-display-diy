// The node's identity: an EC P-256 key and a self-signed certificate for its HTTPS endpoint,
// and a random token that callers must send. Made on first start and kept in flash, so the
// server's pinned certificate stays valid. The private key never leaves the node.
#include <Preferences.h>
#include <esp_random.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/ecp.h>
#include <mbedtls/entropy.h>
#include <mbedtls/pk.h>
#include <mbedtls/x509_crt.h>

#include "app.h"

namespace node {

namespace {

const char* kNamespace = "akashic";

String randomToken() {
  static const char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
  String t;
  for (int i = 0; i < 43; i++) t += kAlphabet[esp_random() & 63];
  return t;
}

/** Make a key and a self-signed certificate (CA:TRUE, so it can be its own trust anchor). */
bool makeCertificate(String& keyPem, String& certPem) {
  mbedtls_entropy_context entropy;
  mbedtls_ctr_drbg_context drbg;
  mbedtls_pk_context key;
  mbedtls_x509write_cert crt;
  mbedtls_mpi serial;
  mbedtls_entropy_init(&entropy);
  mbedtls_ctr_drbg_init(&drbg);
  mbedtls_pk_init(&key);
  mbedtls_x509write_crt_init(&crt);
  mbedtls_mpi_init(&serial);
  static unsigned char buf[2048];
  bool ok = false;
  do {
    const char* pers = "akashic-sensor-node";
    if (mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &entropy, reinterpret_cast<const unsigned char*>(pers), strlen(pers))) break;
    if (mbedtls_pk_setup(&key, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY))) break;
    if (mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1, mbedtls_pk_ec(key), mbedtls_ctr_drbg_random, &drbg)) break;
    unsigned char sn[16];
    esp_fill_random(sn, sizeof sn);
    sn[0] &= 0x7F;
    if (mbedtls_mpi_read_binary(&serial, sn, sizeof sn)) break;
    mbedtls_x509write_crt_set_version(&crt, MBEDTLS_X509_CRT_VERSION_3);
    mbedtls_x509write_crt_set_md_alg(&crt, MBEDTLS_MD_SHA256);
    mbedtls_x509write_crt_set_subject_key(&crt, &key);
    mbedtls_x509write_crt_set_issuer_key(&crt, &key);
    const String name = String("CN=akashic-sensor-") + NODE_NAME;
    if (mbedtls_x509write_crt_set_subject_name(&crt, name.c_str())) break;
    if (mbedtls_x509write_crt_set_issuer_name(&crt, name.c_str())) break;
    if (mbedtls_x509write_crt_set_serial(&crt, &serial)) break;
    if (mbedtls_x509write_crt_set_validity(&crt, "20250101000000", "20491231235959")) break;
    if (mbedtls_x509write_crt_set_basic_constraints(&crt, 1, 0)) break;
    if (mbedtls_x509write_crt_pem(&crt, buf, sizeof buf, mbedtls_ctr_drbg_random, &drbg)) break;
    certPem = reinterpret_cast<char*>(buf);
    if (mbedtls_pk_write_key_pem(&key, buf, sizeof buf)) break;
    keyPem = reinterpret_cast<char*>(buf);
    ok = true;
  } while (false);
  memset(buf, 0, sizeof buf);
  mbedtls_mpi_free(&serial);
  mbedtls_x509write_crt_free(&crt);
  mbedtls_pk_free(&key);
  mbedtls_ctr_drbg_free(&drbg);
  mbedtls_entropy_free(&entropy);
  return ok;
}

}  // namespace

bool loadIdentity() {
  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) return false;
  identity.keyPem = prefs.getString("key", "");
  identity.certPem = prefs.getString("cert", "");
  identity.token = prefs.getString("token", "");
  if (identity.token.length() < 32) {
    identity.token = randomToken();
    prefs.putString("token", identity.token);
  }
  if (identity.keyPem.isEmpty() || identity.certPem.isEmpty()) {
    Serial.println("Making this node's certificate (first start)...");
    if (!makeCertificate(identity.keyPem, identity.certPem)) {
      prefs.end();
      identity.keyPem = identity.certPem = "";
      return false;
    }
    prefs.putString("key", identity.keyPem);
    prefs.putString("cert", identity.certPem);
  }
  prefs.end();
  // For calling it from your own network; the server is sent it over its own connection.
  Serial.printf("GraphQL token: %s\n", identity.token.c_str());
  return true;
}

}  // namespace node
