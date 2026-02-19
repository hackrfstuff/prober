#if defined(_WIN32)

/*
 * Copyright (c) 2014 Craig Lilley <cralilley@gmail.com>
 * This software is made available under the terms of the MIT licence.
 * A copy of the licence can be obtained from:
 * http://opensource.org/licenses/MIT
 */

#include "serial/serial.h"
#include <tchar.h>
#include <windows.h>
#include <setupapi.h>
#include <initguid.h>
#include <devguid.h>
#include <cfgmgr32.h>
#include <cstring>
#include <map>
#include <algorithm>

// {86E0D1E0-8089-11D0-9CE4-08003E301F73} GUID_DEVINTERFACE_COMPORT
DEFINE_GUID(GUID_DEVINTERFACE_COMPORT_LOCAL,
    0x86E0D1E0, 0x8089, 0x11D0,
    0x9C, 0xE4, 0x08, 0x00, 0x3E, 0x30, 0x1F, 0x73);

using serial::PortInfo;
using std::vector;
using std::string;
using std::map;

static const DWORD friendly_name_max_length = 256;
static const DWORD hardware_id_max_length = 256;
static const DWORD port_name_max_length = 256;

static string tchar_to_utf8(const TCHAR* s)
{
#ifdef UNICODE
	if (!s || !s[0]) return string();
	int len = WideCharToMultiByte(CP_UTF8, 0, s, -1, NULL, 0, NULL, NULL);
	if (len <= 0) return string();
	string out(len - 1, 0);
	WideCharToMultiByte(CP_UTF8, 0, s, -1, &out[0], len, NULL, NULL);
	return out;
#else
	return s ? string(s) : string();
#endif
}

static void enumerate_querydosdevice(map<string, PortInfo>& ports)
{
	DWORD buf_size = 65535;
	vector<TCHAR> buf(buf_size);

	DWORD len = QueryDosDevice(NULL, buf.data(), buf_size);
	if (len == 0) return;

	const TCHAR* ptr = buf.data();
	while (*ptr) {
		string name = tchar_to_utf8(ptr);
		ptr += _tcslen(ptr) + 1;

		if (name.compare(0, 3, "COM") != 0) continue;
		if (name.size() <= 3) continue;
		bool all_digits = true;
		for (size_t i = 3; i < name.size(); i++)
			if (!isdigit((unsigned char)name[i])) { all_digits = false; break; }
		if (!all_digits) continue;

		if (ports.find(name) == ports.end()) {
			PortInfo pi;
			pi.port = name;
			ports[name] = pi;
		}
	}
}

static void enrich_from_setupapi(map<string, PortInfo>& ports)
{
	HDEVINFO devs = SetupDiGetClassDevs(
		(const GUID*)&GUID_DEVCLASS_PORTS, NULL, NULL, DIGCF_PRESENT);
	if (devs == INVALID_HANDLE_VALUE) return;

	SP_DEVINFO_DATA did;
	did.cbSize = sizeof(did);

	for (DWORD i = 0; SetupDiEnumDeviceInfo(devs, i, &did); i++) {
		HKEY hkey = SetupDiOpenDevRegKey(devs, &did,
			DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
		if (hkey == INVALID_HANDLE_VALUE) continue;

		TCHAR pname[port_name_max_length];
		DWORD pname_len = sizeof(pname);
		LONG rc = RegQueryValueEx(hkey, _T("PortName"), NULL, NULL,
			(LPBYTE)pname, &pname_len);
		RegCloseKey(hkey);
		if (rc != ERROR_SUCCESS) continue;

		string port = tchar_to_utf8(pname);
		auto it = ports.find(port);
		if (it == ports.end()) {
			PortInfo pi;
			pi.port = port;
			ports[port] = pi;
			it = ports.find(port);
		}

		TCHAR friendly[friendly_name_max_length];
		DWORD friendly_len = 0;
		if (SetupDiGetDeviceRegistryProperty(devs, &did, SPDRP_FRIENDLYNAME,
				NULL, (PBYTE)friendly, sizeof(friendly), &friendly_len))
			it->second.description = tchar_to_utf8(friendly);

		TCHAR hwid[hardware_id_max_length];
		DWORD hwid_len = 0;
		if (SetupDiGetDeviceRegistryProperty(devs, &did, SPDRP_HARDWAREID,
				NULL, (PBYTE)hwid, sizeof(hwid), &hwid_len))
			it->second.hardware_id = tchar_to_utf8(hwid);
	}

	SetupDiDestroyDeviceInfoList(devs);
}

// fallback: HKLM\HARDWARE\DEVICEMAP\SERIALCOMM
static void enumerate_registry(map<string, PortInfo>& ports)
{
	HKEY key;
	if (RegOpenKeyEx(HKEY_LOCAL_MACHINE,
			_T("HARDWARE\\DEVICEMAP\\SERIALCOMM"),
			0, KEY_READ, &key) != ERROR_SUCCESS)
		return;

	for (DWORD i = 0; ; i++) {
		TCHAR valueName[256], valueData[256];
		DWORD nameLen = 256, dataLen = sizeof(valueData), type = 0;
		LONG rc = RegEnumValue(key, i, valueName, &nameLen,
			NULL, &type, (LPBYTE)valueData, &dataLen);
		if (rc != ERROR_SUCCESS) break;
		if (type != REG_SZ) continue;

		string port = tchar_to_utf8(valueData);
		if (!port.empty() && ports.find(port) == ports.end()) {
			PortInfo pi;
			pi.port = port;
			ports[port] = pi;
		}
	}

	RegCloseKey(key);
}

// some USB-serial adapters register GUID_DEVINTERFACE_COMPORT but not GUID_DEVCLASS_PORTS
static void enrich_from_devinterface(map<string, PortInfo>& ports)
{
	HDEVINFO devs = SetupDiGetClassDevs(
		&GUID_DEVINTERFACE_COMPORT_LOCAL, NULL, NULL,
		DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
	if (devs == INVALID_HANDLE_VALUE) return;

	SP_DEVICE_INTERFACE_DATA ifd;
	ifd.cbSize = sizeof(ifd);

	for (DWORD i = 0; SetupDiEnumDeviceInterfaces(devs, NULL,
			&GUID_DEVINTERFACE_COMPORT_LOCAL, i, &ifd); i++) {

		SP_DEVINFO_DATA did;
		did.cbSize = sizeof(did);

		DWORD needed = 0;
		SetupDiGetDeviceInterfaceDetail(devs, &ifd, NULL, 0, &needed, &did);
		if (needed == 0) continue;

		vector<BYTE> detailBuf(needed);
		SP_DEVICE_INTERFACE_DETAIL_DATA* detail =
			reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA*>(detailBuf.data());
		detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);

		if (!SetupDiGetDeviceInterfaceDetail(devs, &ifd, detail, needed, NULL, &did))
			continue;

		HKEY hkey = SetupDiOpenDevRegKey(devs, &did,
			DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
		if (hkey == INVALID_HANDLE_VALUE) continue;

		TCHAR pname[port_name_max_length];
		DWORD pname_len = sizeof(pname);
		LONG rc = RegQueryValueEx(hkey, _T("PortName"), NULL, NULL,
			(LPBYTE)pname, &pname_len);
		RegCloseKey(hkey);
		if (rc != ERROR_SUCCESS) continue;

		string port = tchar_to_utf8(pname);
		if (port.empty() || port.compare(0, 3, "COM") != 0) continue;

		auto it = ports.find(port);
		if (it == ports.end()) {
			PortInfo pi;
			pi.port = port;
			ports[port] = pi;
			it = ports.find(port);
		}

		if (it->second.description.empty()) {
			TCHAR friendly[friendly_name_max_length];
			if (SetupDiGetDeviceRegistryProperty(devs, &did, SPDRP_FRIENDLYNAME,
					NULL, (PBYTE)friendly, sizeof(friendly), NULL))
				it->second.description = tchar_to_utf8(friendly);
		}
		if (it->second.hardware_id.empty()) {
			TCHAR hwid[hardware_id_max_length];
			if (SetupDiGetDeviceRegistryProperty(devs, &did, SPDRP_HARDWAREID,
					NULL, (PBYTE)hwid, sizeof(hwid), NULL))
				it->second.hardware_id = tchar_to_utf8(hwid);
		}
	}

	SetupDiDestroyDeviceInfoList(devs);
}

static bool port_sort(const PortInfo& a, const PortInfo& b)
{
	int na = 0, nb = 0;
	if (a.port.size() > 3) na = atoi(a.port.c_str() + 3);
	if (b.port.size() > 3) nb = atoi(b.port.c_str() + 3);
	return na < nb;
}

vector<PortInfo>
serial::list_ports()
{
	map<string, PortInfo> ports;

	enumerate_querydosdevice(ports);
	enrich_from_setupapi(ports);
	enrich_from_devinterface(ports);
	enumerate_registry(ports);

	vector<PortInfo> result;
	result.reserve(ports.size());
	for (auto& kv : ports) {
		if (kv.second.port.find("LPT") != string::npos) continue;
		result.push_back(kv.second);
	}

	std::sort(result.begin(), result.end(), port_sort);
	return result;
}

#endif // #if defined(_WIN32)
