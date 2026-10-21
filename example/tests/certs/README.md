# Localhost benchmark certificate

These are deliberately public, disposable test credentials for the example HTTP/TLS fixture. The certificate has SANs localhost and 127.0.0.1. Its private key is not a production credential. Only the example Release package trusts this certificate, and only for those loopback hosts; the published library does not add trust roots or disable hostname validation.
