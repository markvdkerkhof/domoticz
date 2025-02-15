#include "stdafx.h"
#include "Winddelen.h"
#include "../main/Helper.h"
#include "../main/Logger.h"
#include "../httpclient/HTTPClient.h"
#include "../httpclient/UrlEncode.h"
#include "hardwaretypes.h"
#include "../main/mainworker.h"
#include "../main/json_helper.h"

#define WINDDELEN_POLL_INTERVAL 30

#ifdef _DEBUG
//#define DEBUG_WindDelenR
//#define DEBUG_WindDelenW
#endif

#if defined(DEBUG_WindDelenW)
void SaveString2Disk(std::string str, std::string filename)
{
	FILE *fOut = fopen(filename.c_str(), "wb+");
	if (fOut)
	{
		fwrite(str.c_str(), 1, str.size(), fOut);
		fclose(fOut);
	}
}
#endif
#ifdef DEBUG_WindDelenR
std::string ReadFile(std::string filename)
{
	std::ifstream file;
	std::string sResult = "";
	file.open(filename.c_str());
	if (!file.is_open())
		return "";
	std::string sLine;
	while (!file.eof())
	{
		getline(file, sLine);
		sResult += sLine;
	}
	file.close();
	return sResult;
}
#endif

CWinddelen::CWinddelen(const int ID, const std::string &IPAddress, const unsigned short usTotParts, const unsigned short usMillID) :
	m_szMillName(IPAddress)
{
	m_HwdID = ID;
	m_usTotParts = usTotParts;
	m_usMillID = usMillID;

	m_winddelen_per_mill[1] = 9910.0;
	m_winddelen_per_mill[2] = 10154.0;
	m_winddelen_per_mill[31] = 6648.0;
	m_winddelen_per_mill[41] = 6164.0;
	m_winddelen_per_mill[51] = 5721.0;
	m_winddelen_per_mill[111] = 5579.0;
	m_winddelen_per_mill[121] = 5602.0;
	m_winddelen_per_mill[131] = 5534.0;
	m_winddelen_per_mill[141] = 5512.0;
	m_winddelen_per_mill[191] = 3000.0;
}

void CWinddelen::Init()
{
}

bool CWinddelen::StartHardware()
{
	RequestStart();

	Init();
	//Start worker thread
	m_thread = std::make_shared<std::thread>([this] { Do_Work(); });
	SetThreadNameInt(m_thread->native_handle());
	m_bIsStarted = true;
	sOnConnected(this);
	return (m_thread != nullptr);
}

bool CWinddelen::StopHardware()
{
	if (m_thread)
	{
		RequestStop();
		m_thread->join();
		m_thread.reset();
	}
	m_bIsStarted = false;
	return true;
}

void CWinddelen::Do_Work()
{
	Log(LOG_STATUS, "Worker started...");

	int sec_counter = WINDDELEN_POLL_INTERVAL - 2;

	while (!IsStopRequested(1000))
	{
		sec_counter++;

		if (sec_counter % 12 == 0) {
			m_LastHeartbeat = mytime(nullptr);
		}
		if (sec_counter % WINDDELEN_POLL_INTERVAL == 0)
		{
			GetMeterDetails();
		}
	}
	Log(LOG_STATUS, "Worker stopped...");
}

bool CWinddelen::WriteToHardware(const char *pdata, const unsigned char length)
{
	return false;
}

void CWinddelen::GetMeterDetails()
{
	std::string sResult;

#ifdef DEBUG_WindDelenR
	sResult = ReadFile("E:\\WindDelen.json");
#else
	char szURL[200];
	sprintf(szURL, "https://zep-api.windcentrale.nl/production/%d/live", m_usMillID);

	if (!HTTPClient::GETSingleLine(szURL, sResult))
	{
		Log(LOG_ERROR, "Error connecting to: %s", szURL);
		return;
	}
#endif
#ifdef DEBUG_WindDelenW
	SaveString2Disk(sResult, "E:\\WindDelen.json");
#endif

	try
	{
		Json::Value root;
		bool ret = ParseJSon(sResult, root);
		if (!ret)
		{
			Log(LOG_ERROR, "Invalid data received!");
			return;
		}

		if (root.empty())
		{
			Log(LOG_ERROR, "Invalid data received!");
			return;
		}
		if (!root.isObject())
		{
			Log(LOG_ERROR, "Invalid data received, or unknown location!");
			return;
		}

		if (root["powerAbsTot"].empty())
		{
			Log(LOG_ERROR, "Invalid data received, or unknown location!");
			return;
		}
		if (root["powerAbsWd"].empty())
		{
			Log(LOG_ERROR, "Invalid data received, or unknown location!");
			return;
		}
		if (root["kwh"].empty())
		{
			Log(LOG_ERROR, "Invalid data received, or unknown location!");
			return;
		}

		if (m_winddelen_per_mill.find(m_usMillID) == m_winddelen_per_mill.end())
		{
			if (root["kwh"].empty())
			{
				Log(LOG_ERROR, "unknown location!");
				return;
			}
		}

		double powerAbsTot = root["powerAbsTot"].asDouble();
		double powerAbsWd = root["powerAbsWd"].asDouble();
		double kwh = root["kwh"].asDouble();

		double powerusage = kwh * m_usTotParts / m_winddelen_per_mill[m_usMillID];
		double usagecurrent = powerAbsWd * m_usTotParts / 1000.0;

		SendKwhMeterOldWay(m_usMillID, 1, 255, usagecurrent, powerusage, "Wind Power");

		float powerRel = root["powerRel"].asFloat();
		SendPercentageSensor(m_usMillID, 1, 255, powerRel, "Wind PowerRel");

		int rpm = root["rpm"].asInt();
		SendFanSensor(m_usMillID, 255, rpm, "Wind RPM");

		if (!root["windSpeed"].empty())
		{
			float windSpeed = root["windSpeed"].asFloat();
			int windDir = 0;
			std::string szWD = root["windDirection"].asString();
			if (szWD == "N")
				windDir = static_cast<int>(std::rint(0 * 22.5F));
			else if ((szWD == "NNE") || (szWD == "NNO"))
				windDir = static_cast<int>(std::rint(1 * 22.5F));
			else if ((szWD == "NE") || (szWD == "NO"))
				windDir = static_cast<int>(std::rint(2 * 22.5F));
			else if ((szWD == "ENE") || (szWD == "ONO"))
				windDir = static_cast<int>(std::rint(3 * 22.5F));
			else if ((szWD == "E") || (szWD == "O"))
				windDir = static_cast<int>(std::rint(4 * 22.5F));
			else if ((szWD == "ESE") || (szWD == "OZO"))
				windDir = static_cast<int>(std::rint(5 * 22.5F));
			else if ((szWD == "SE") || (szWD == "ZO"))
				windDir = static_cast<int>(std::rint(6 * 22.5F));
			else if ((szWD == "SSE") || (szWD == "ZZO"))
				windDir = static_cast<int>(std::rint(7 * 22.5F));
			else if ((szWD == "S") || (szWD == "Z"))
				windDir = static_cast<int>(std::rint(8 * 22.5F));
			else if ((szWD == "SSW") || (szWD == "ZZW"))
				windDir = static_cast<int>(std::rint(9 * 22.5F));
			else if ((szWD == "SW") || (szWD == "ZW"))
				windDir = static_cast<int>(std::rint(10 * 22.5F));
			else if ((szWD == "WSW") || (szWD == "WZW"))
				windDir = static_cast<int>(std::rint(11 * 22.5F));
			else if ((szWD == "SSW") || (szWD == "ZZW"))
				windDir = static_cast<int>(std::rint(12 * 22.5F));
			else if (szWD == "WNW")
				windDir = static_cast<int>(std::rint(13 * 22.5F));
			else if (szWD == "NW")
				windDir = static_cast<int>(std::rint(14 * 22.5F));
			else if (szWD == "NNW")
				windDir = static_cast<int>(std::rint(15 * 22.5F));

			SendWind(m_usMillID, 255, windDir, windSpeed, windSpeed, 0, 0, false, false, "Wind");
		}

		if (!root["diameter"].empty())
		{
			float diameter = root["diameter"].asFloat();
			SendCustomSensor(m_usMillID, 1, 255, diameter, "Wind Diameter", "meter");
		}
	}
	catch (...)
	{
		Log(LOG_ERROR, "Error parsing JSon data!");
	}


	std::vector<std::string> results;
	StringSplit(sResult, ",", results);
	if (results.size() < 7)
	{
		Log(LOG_ERROR, "Invalid response for '%s'", m_szMillName.c_str());
		return;
	}

	int fpos;
	std::string pusage = stdstring_trim(results[7]);
	fpos = pusage.find_first_of(' ');
	if (fpos != std::string::npos)
		pusage = pusage.substr(0, fpos);
	stdreplace(pusage, ",", "");

	std::string pcurrent = stdstring_trim(results[2]);
	fpos = pcurrent.find_first_of(' ');
	if (fpos != std::string::npos)
		pcurrent = pcurrent.substr(0, fpos);
	stdreplace(pcurrent, ",", "");


}
