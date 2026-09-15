#include <string>

class Encryption
{

public:
    std::string Encrypt(std::string raw, std::string key);
    std::string Decrypt(std::string raw, std::string key);
    std::string RandomKey();
};
