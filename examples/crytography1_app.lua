local crypto = require("LumeniteCrypto")






-- This does nto demo the Framework really, but shows off the Cryptography Module









function test_crypto()
    local ok = true

    local function check(label, cond)
        if cond then
            print("[+]    " .. label)
        else
            print("[-]    " .. label)
            ok = false
        end
    end

    -- Test cases for hashing & verifying
    local test_passwords = {
        { label = "normal password", pw = "hunter2", should_pass = true },
        { label = "empty password", pw = "", should_pass = true },
        { label = "long password", pw = string.rep("a", 1000), should_pass = true },
        { label = "unicode password", pw = "pässwörd♥", should_pass = true },
        { label = "wrong password match", pw = "wrongpw", ref = "hunter2", should_pass = false },
    }

    for _, case in ipairs(test_passwords) do
        local pw = case.pw
        local ref = case.ref or pw
        local hash = crypto.hash(ref)
        local result = crypto.verify(pw, hash)
        check("crypto.verify " .. case.label, result == case.should_pass)
    end

    -- AES encryption/decryption tests
    local good_key = string.rep("K", 32)
    local bad_key = string.rep("X", 32)
    local short_key = "badkey"
    local payloads = {
        { label = "empty string", data = "" },
        { label = "short string", data = "Hello" },
        { label = "unicode string", data = "Привет мир" },
        { label = "long data", data = string.rep("X", 2048) },
        { label = "binary content", data = string.char(0, 1, 2, 255, 128, 64) },
    }

    for _, case in ipairs(payloads) do
        local encrypted = crypto.encrypt(good_key, case.data)
        local decrypted = crypto.decrypt(good_key, encrypted)
        check("crypto.decrypt " .. case.label, decrypted == case.data)
    end

    -- Decryption with wrong key (should return garbage, not match)
    local encrypted = crypto.encrypt(good_key, "supersecret")
    local bad_result = crypto.decrypt(bad_key, encrypted)
    check("decrypt with wrong key (should not match)", bad_result ~= "supersecret")

    -- Attempt to use a bad key length (should throw)
    local success, err = pcall(function()
        crypto.encrypt(short_key, "data")
    end)
    print(success, err)
    check("encrypt with short key should error", not success and err:match("32 bytes"))

    -- Corrupt ciphertext by altering last bytes
    local valid = crypto.encrypt(good_key, "msg")
    local corrupted = valid:sub(1, -3) .. "\x00\x00"

    local corrupted_ok, corrupted_result = pcall(function()
        return crypto.decrypt(good_key, corrupted)
    end)
    print(corrupted_ok, corrupted_result)
    check("decrypt corrupted data should fail", not corrupted_ok)

    if ok then
        print("[+] All crypto tests passed")
    else
        print("[ :( ] Some crypto tests failed")
    end

    return ok
end

test_crypto()


