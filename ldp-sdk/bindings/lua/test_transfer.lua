local ldp = require("ldp")

local function pattern(size)
    local bytes = {}
    for i = 0, size - 1 do bytes[#bytes + 1] = string.char(i % 256) end
    return table.concat(bytes)
end

assert(ldp.version() == "0.2.0")
local payload = pattern(6000)
assert(ldp.transfer_loopback(payload, { chunk_size = 512, rekey_every = 4 }) == payload)
local hybrid = pattern(2048)
assert(ldp.transfer_loopback(hybrid, { profile = ldp.PROFILE_HYBRID, chunk_size = 256 }) == hybrid)
local identity = ldp.identity_generate()
assert(#identity.public_key == 1952 and #identity.secret_key == 4032)
print("lua ldp tests passed")
