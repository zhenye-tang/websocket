#ifndef __TLS_CERTIFICATE_H__
#define __TLS_CERTIFICATE_H__

#include <stddef.h>

#define MBEDTLS_CERTIFICATE    \
"-----BEGIN CERTIFICATE-----\r\n" \
"MIIDCTCCAfGgAwIBAgIUdUzMXIb4qpThAjUGKeVLHEXsDnswDQYJKoZIhvcNAQEL\r\n" \
"BQAwFDESMBAGA1UEAwwJMTI3LjAuMC4xMB4XDTE5MDkzMDAzNDMwMVoXDTIxMDky\r\n" \
"OTAzNDMwMVowFDESMBAGA1UEAwwJMTI3LjAuMC4xMIIBIjANBgkqhkiG9w0BAQEF\r\n" \
"AAOCAQ8AMIIBCgKCAQEAtrrD5aNW/Ffv3sYuQYxbGchFSEEK4V+6M1sLpKPOK3K7\r\n" \
"TIXPgdrt1IraRInim3WedRGnZOxDQAsHJ1elZ7tZ4PmeoXEuOOMoziHyQfT4xMLu\r\n" \
"5a0SL0yfhe/r/lM4YbdlIlM8dHQLjhGI3IEUIwBvctUSL3u6ZNiQhKzwHFlLBlJ9\r\n" \
"g+VtawoYdYvEJetakBG+QdgodGT3FrXy6ZI7mORN0J6aU6QHBVkjehaPbeu5+pYn\r\n" \
"O8ogQG6Rnp+Y+LeLg9DMLQ84gEGLoO89r9lKPFVsnqt3KQiGaEw5aV3vvJBmlwwO\r\n" \
"xf/lyPRoaVoMkEAFZEQM+0YVVLaMi4mNuahTDPhV8QIDAQABo1MwUTAdBgNVHQ4E\r\n" \
"FgQUiTsCNkdY8tNynnIMJUzaYbs/2p4wHwYDVR0jBBgwFoAUiTsCNkdY8tNynnIM\r\n" \
"JUzaYbs/2p4wDwYDVR0TAQH/BAUwAwEB/zANBgkqhkiG9w0BAQsFAAOCAQEAa+jt\r\n" \
"gPLzn5wETHlETu5unnR4LR3cdqa8qLLKU5RNZE3JroQnH12fS73PMrgh8Ds51Qmi\r\n" \
"Q4d3VjzHbIew5EW3TyDFKLOXdXk1rPRlk5PWvIaJHIn7i6vxFe6YeOS0L2o25CBJ\r\n" \
"Mthj3bSdssBM0CPZ6BQEhcn/qDcRE7IbggEkjyHd5hnJVCP/cQW1vhuIi2pef6nb\r\n" \
"m38v6cGxrazDUnBOfZdwb2eqcCKgblDtsiMhZI0XfyAXiSwgl2MEMl8KTHl6TSVo\r\n" \
"2CzYHgkQUlTbyILDRgSuOEnp9+6ifafeL2M4iGfBUcZuJfE67766bs3OMlrKkFOt\r\n" \
"RKhxOu2pW45w0CkHOQ==\r\n" \
"-----END CERTIFICATE-----\r\n" \


extern const char mbedtls_certificate[];
extern const size_t mbedtls_certificate_len;

#endif //__TLS_CERTIFICATE_H__