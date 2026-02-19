#if defined(__linux__)

/*
 * Copyright (c) 2014 Craig Lilley <cralilley@gmail.com>
 * This software is made available under the terms of the MIT licence.
 * A copy of the licence can be obtained from:
 * http://opensource.org/licenses/MIT
 */

#include <vector>
#include <string>
#include <fstream>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>

#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "serial/serial.h"

using serial::PortInfo;
using std::vector;
using std::string;

static string read_line(const string& path)
{
    std::ifstream ifs(path.c_str());
    string line;
    if (ifs) std::getline(ifs, line);
    return line;
}

static bool path_exists(const string& path)
{
    struct stat sb;
    return stat(path.c_str(), &sb) == 0;
}

static string resolve_symlink(const string& path)
{
    char* rp = ::realpath(path.c_str(), NULL);
    if (!rp) return string();
    string result(rp);
    free(rp);
    return result;
}

static string dirname(const string& path)
{
    size_t pos = path.rfind('/');
    if (pos == string::npos) return path;
    if (pos == 0) return "/";
    return path.substr(0, pos);
}

static string find_usb_device_path(const string& tty_name, const string& device_link)
{
    string resolved = resolve_symlink(device_link);
    if (resolved.empty()) return string();

    if (tty_name.compare(0, 6, "ttyUSB") == 0)
        return dirname(dirname(resolved));
    if (tty_name.compare(0, 6, "ttyACM") == 0)
        return dirname(resolved);
    // walk up looking for idVendor
    string p = resolved;
    for (int i = 0; i < 5 && !p.empty() && p != "/"; i++) {
        if (path_exists(p + "/idVendor")) return p;
        p = dirname(p);
    }
    return string();
}

static string usb_friendly_name(const string& usb_path)
{
    string mfr = read_line(usb_path + "/manufacturer");
    string prod = read_line(usb_path + "/product");
    if (mfr.empty() && prod.empty()) return string();
    string result;
    if (!mfr.empty()) result = mfr;
    if (!prod.empty()) {
        if (!result.empty()) result += " ";
        result += prod;
    }
    return result;
}

static string usb_hw_string(const string& usb_path)
{
    string vid = read_line(usb_path + "/idVendor");
    string pid = read_line(usb_path + "/idProduct");
    string serial = read_line(usb_path + "/serial");
    if (vid.empty() && pid.empty()) return "n/a";
    char buf[128];
    if (serial.empty())
        snprintf(buf, sizeof(buf), "USB VID:PID=%s:%s", vid.c_str(), pid.c_str());
    else
        snprintf(buf, sizeof(buf), "USB VID:PID=%s:%s SNR=%s",
                 vid.c_str(), pid.c_str(), serial.c_str());
    return buf;
}

// ttyS* ports on pnp0/platform are phantom; only usb/pci are real hardware.
static bool device_has_real_bus(const string& device_link)
{
    string resolved = resolve_symlink(device_link);
    if (resolved.empty()) return false;
    return resolved.find("/usb") != string::npos ||
           resolved.find("/pci") != string::npos;
}

vector<PortInfo>
serial::list_ports()
{
    vector<PortInfo> results;
    const string sysfs_tty = "/sys/class/tty";

    DIR* dir = opendir(sysfs_tty.c_str());
    if (!dir) return results;

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        string name = entry->d_name;
        if (name == "." || name == "..") continue;

        string device_link = sysfs_tty + "/" + name + "/device";

        if (!path_exists(device_link)) continue;

        // skip phantom ttyS* (pnp/platform) ports
        if (name.compare(0, 4, "ttyS") == 0) {
            if (!device_has_real_bus(device_link)) continue;
        }

        string dev_path = "/dev/" + name;
        PortInfo pi;
        pi.port = dev_path;
        pi.description = name;
        pi.hardware_id = "n/a";

        string usb_path = find_usb_device_path(name, device_link);
        if (!usb_path.empty()) {
            string friendly = usb_friendly_name(usb_path);
            if (!friendly.empty()) pi.description = friendly;
            pi.hardware_id = usb_hw_string(usb_path);
        } else {
            string id_path = device_link + "/id";
            if (path_exists(id_path))
                pi.hardware_id = read_line(id_path);
        }

        results.push_back(pi);
    }

    closedir(dir);

    std::sort(results.begin(), results.end(),
        [](const PortInfo& a, const PortInfo& b) { return a.port < b.port; });

    return results;
}

#endif // defined(__linux__)
