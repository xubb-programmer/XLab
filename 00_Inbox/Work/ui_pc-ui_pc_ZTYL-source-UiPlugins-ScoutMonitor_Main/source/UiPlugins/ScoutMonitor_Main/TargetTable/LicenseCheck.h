#ifndef LICENSECHECK_H
#define LICENSECHECK_H

#include <string>

#  ifdef __linux__
#    define DECL_EXPORT     __attribute__((visibility("default")))
#  else
#    define DECL_EXPORT     __declspec(dllexport)
#endif

class LicenseCheck
{
public:
    static LicenseCheck *getInstance();

    bool result(std::string *error);

private:
    LicenseCheck(std::string exe);
    LicenseCheck(const LicenseCheck&) = delete;
    LicenseCheck& operator=(const LicenseCheck&) = delete;
    ~LicenseCheck();

private:
    void startTimeUpdate();
    void stopTimeUpdate();

private:
    std::string m_exe;
};

extern "C"
{
    DECL_EXPORT bool regist(std::string *error = NULL);
}



#endif // LICENSECHECK_H
