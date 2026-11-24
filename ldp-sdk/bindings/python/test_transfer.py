import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

import ldp


def pattern(size):
    return bytes(i & 0xFF for i in range(size))


def main():
    assert ldp.version() == "0.2.0"
    classic = ldp.default_options()
    classic.chunk_size = 512
    classic.rekey_every = 4
    payload = pattern(6000)
    assert ldp.transfer_loopback(payload, classic) == payload

    hybrid = ldp.default_options()
    hybrid.profile = ldp.PROFILE_HYBRID
    hybrid.chunk_size = 256
    assert ldp.transfer_loopback(pattern(2048), hybrid) == pattern(2048)

    with ldp.Pipe() as pipe:
        server = pipe.session(1, ldp.ROLE_SERVER, ldp.PROFILE_CLASSIC)
        client = pipe.session(0, ldp.ROLE_CLIENT, ldp.PROFILE_CLASSIC)
        body = pattern(3000)
        assert ldp.exchange(server, client, body, 400, 2) == body
        server.close()
        client.close()

    client_id = ldp.Identity.generate()
    server_id = ldp.Identity.generate()
    try:
        with ldp.Pipe() as pipe:
            server = pipe.session(1, ldp.ROLE_SERVER, ldp.PROFILE_HYBRID)
            client = pipe.session(0, ldp.ROLE_CLIENT, ldp.PROFILE_HYBRID)
            server.set_identity(server_id.secret_key, client_id.public_key)
            client.set_identity(client_id.secret_key, server_id.public_key)
            body = pattern(1500)
            assert ldp.exchange(server, client, body, 300, 0) == body
            server.close()
            client.close()
    finally:
        client_id.wipe()
        server_id.wipe()

    row = ldp.measure_loopback(pattern(4096), ldp.default_options())
    assert row["match"] and row["profile"] == "classic" and row["transport"] == "memory"
    text = ldp.format_public_bench([row])
    assert "ldp-sdk bench v1" in text and "match" in text
    print("python ldp tests passed")


if __name__ == "__main__":
    main()
