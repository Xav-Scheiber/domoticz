#include "stdafx.h"
#include "../main/Logger.h"
#include "../main/Helper.h"
#include "../main/SQLHelper.h"
#include "../main/RFXtrx.h"
#include "../main/mainworker.h"
#include "../main/WebServer.h"
#include "../hardware/DomoticzHardware.h"
#include "../hardware/hardwaretypes.h"
#include "NotificationHelper.h"
#include "NotificationProwl.h"
#include "NotificationPushbullet.h"
#include "NotificationPushover.h"
#include "NotificationPushsafer.h"
#include "NotificationPushalot.h"
#include "NotificationEmail.h"
#include "NotificationTelegram.h"
#include "NotificationSMS.h"
#include "NotificationHTTP.h"
#include "NotificationKodi.h"
#include "NotificationLogitechMediaServer.h"
#include "NotificationFCM.h"

#include "NotificationBrowser.h"
#define __STDC_FORMAT_MACROS
#include <inttypes.h>

#if defined WIN32
	#include "../msbuild/WindowsHelper.h"
#endif

extern std::string szUserDataFolder;

using namespace http::server;

CNotificationHelper::CNotificationHelper()
{
	m_NotificationSwitchInterval = 0;
	m_NotificationSensorInterval = 12 * 3600;

	/* more notifiers can be added here */

	AddNotifier(new CNotificationProwl());
	AddNotifier(new CNotificationPushbullet());
	AddNotifier(new CNotificationTelegram());
	AddNotifier(new CNotificationPushover());
	AddNotifier(new CNotificationPushsafer());
	AddNotifier(new CNotificationPushalot());
	AddNotifier(new CNotificationEmail());
	AddNotifier(new CNotificationSMS());
	AddNotifier(new CNotificationHTTP());
	AddNotifier(new CNotificationKodi());
	AddNotifier(new CNotificationLogitechMediaServer());
	AddNotifier(new CNotificationFCM());
	AddNotifier(new CNotificationBrowser());
}

CNotificationHelper::~CNotificationHelper()
{
	for (auto &m_notifier : m_notifiers)
		delete m_notifier.second;
}

void CNotificationHelper::Init()
{
	ReloadNotifications();
}

void CNotificationHelper::AddNotifier(CNotificationBase *notifier)
{
	m_notifiers[notifier->GetSubsystemId()] = notifier;
}

void CNotificationHelper::RemoveNotifier(CNotificationBase *notifier)
{
	m_notifiers.erase(notifier->GetSubsystemId());
}

bool CNotificationHelper::SendMessage(
	const uint64_t Idx,
	const std::string &Name,
	const std::string &Subsystems,
	const std::string& CustomAction,
	const std::string &Subject,
	const std::string &Text,
	const std::string &ExtraData,
	const int Priority,
	const std::string &Sound,
	const bool bFromNotification)
{
	return SendMessageEx(Idx, Name, Subsystems, CustomAction, Subject, Text, ExtraData, -100, std::string(""), bFromNotification);
}

bool CNotificationHelper::SendMessageEx(
	const uint64_t Idx,
	const std::string& Name,
	const std::string& Subsystems,
	const std::string& CustomAction,
	const std::string& Subject,
	const std::string& Text,
	const std::string& ExtraData,
	int Priority,
	const std::string& Sound,
	const bool bFromNotification)
{
	bool bRet = false;
	bool bThread = true;

	bool bIsTestMessage = (Subject == "Domoticz test") && (Text == "Domoticz test message!");

	if (Priority == -100)
	{
		Priority = 0;
		bThread = false;
		bRet = true;
	}

#if defined WIN32
	//Make a system tray message
	ShowSystemTrayNotification(Subject.c_str());
#endif
	_log.Log(LOG_STATUS, "Notification: %s", Subject.c_str());

	std::vector<std::string> sResult;
	StringSplit(Subsystems, ";", sResult);

	std::map<std::string, int> ActiveSystems;

	for (const auto &result : sResult)
		ActiveSystems[result] = 1;

	for (const auto &m_notifier : m_notifiers)
	{
		auto ittSystem = ActiveSystems.find(m_notifier.first);
		if ((ActiveSystems.empty() || ittSystem != ActiveSystems.end()) && m_notifier.second->IsConfigured())
		{
			if ((m_notifier.second->m_IsEnabled) || bIsTestMessage)
			{
				if (bThread)
				{
					boost::thread SendMessageEx([=] { m_notifier.second->SendMessageEx(Idx, Name, Subject, Text, ExtraData, Priority, Sound, bFromNotification); });
					SetThreadName(SendMessageEx.native_handle(), "SendMessageEx");
				}
				else
					bRet |= m_notifier.second->SendMessageEx(Idx, Name, Subject, Text, ExtraData, Priority, Sound,
										 bFromNotification);
			}
		}
	}
	if (!CustomAction.empty())
	{
		std::string Action = CustomAction;
		stdreplace(Action, "#MESSAGE", Subject);

		if ((Action.find("http://") == 0) || (Action.find("https://") == 0))
		{
			m_sql.AddTaskItem(_tTaskItem::GetHTTPPage(0.2F, Action, "Notification"));
		}
		else if (Action.find("script://") == 0)
		{
			//Execute possible script
			if (Action.find("../") != std::string::npos)
			{
				_log.Log(LOG_ERROR, "Notification: Invalid script location! '%s'", Action.c_str());
				return false;
			}

			std::string scriptname = Action.substr(9);
#if !defined WIN32
			if (scriptname.find('/') != 0)
				scriptname = szUserDataFolder + "scripts/" + scriptname;
#endif
			std::string scriptparams;
			//Add parameters
			size_t pindex = scriptname.find(' ');
			if (pindex != std::string::npos)
			{
				scriptparams = scriptname.substr(pindex + 1);
				scriptname = scriptname.substr(0, pindex);
			}
			if (file_exist(scriptname.c_str()))
			{
				m_sql.AddTaskItem(_tTaskItem::ExecuteScript(0.2F, scriptname, scriptparams));
			}
			else
				_log.Log(LOG_ERROR, "Notification: Error script not found '%s'", scriptname.c_str());
		}
	}
	return bRet;
}

void CNotificationHelper::SetConfigValue(const std::string &key, const std::string &value)
{
	for (const auto &m_notifier : m_notifiers)
		m_notifier.second->SetConfigValue(key, value);
}

void CNotificationHelper::ConfigFromGetvars(const request& req, const bool save)
{
	for (const auto &m_notifier : m_notifiers)
		m_notifier.second->ConfigFromGetvars(req, save);
}

bool CNotificationHelper::IsInConfig(const std::string &Key)
{
	return std::any_of(m_notifiers.begin(), m_notifiers.end(),
			   [&](std::pair<const std::string, CNotificationBase *> &n) { return n.second->IsInConfig(Key); });
}

void CNotificationHelper::LoadConfig()
{
	int tot = 0, active = 0;
	std::stringstream logline;
	logline << "Active notification Subsystems:";
	for (const auto &m_notifier : m_notifiers)
	{
		tot++;
		m_notifier.second->LoadConfig();
		if (m_notifier.second->IsConfigured())
		{
			if (m_notifier.second->m_IsEnabled)
			{
				if (active == 0)
					logline << " " << m_notifier.first;
				else
					logline << ", " << m_notifier.first;
				active++;
			}
		}
	}
	logline << " (" << active << "/" << tot << ")";
	_log.Log(LOG_NORM, logline.str());
}

std::string CNotificationHelper::ParseCustomMessage(const std::string &cMessage, const std::string &sName, const std::string &sValue)
{
	std::string ret = cMessage;
	stdreplace(ret, "$name", sName);
	stdreplace(ret, "$value", sValue);
	return ret;
}

bool CNotificationHelper::ApplyRule(const std::string &rule, const bool equal, const bool less)
{
	if (((rule == ">") || (rule == ">=")) && (!less) && (!equal))
		return true;
	if (((rule == "<") || (rule == "<=")) && (less))
		return true;
	if (((rule == "=") || (rule == ">=") || (rule == "<=")) && (equal))
		return true;
	if ((rule == "!=") && (!equal))
		return true;
	return false;
}

bool CNotificationHelper::CheckAndHandleNotification(const uint64_t DevRowIdx, const int HardwareID, const std::string &ID, const std::string &sName, const unsigned char unit, const unsigned char cType, const unsigned char cSubType, const int nValue) {
	return CheckAndHandleNotification(DevRowIdx, HardwareID, ID, sName, unit, cType, cSubType, nValue, "", 0.0F);
}

bool CNotificationHelper::CheckAndHandleNotification(const uint64_t DevRowIdx, const int HardwareID, const std::string &ID, const std::string &sName, const unsigned char unit, const unsigned char cType, const unsigned char cSubType, const float fValue) {
	return CheckAndHandleNotification(DevRowIdx, HardwareID, ID, sName, unit, cType, cSubType, 0, "", fValue);
}

bool CNotificationHelper::CheckAndHandleNotification(const uint64_t DevRowIdx, const int HardwareID, const std::string &ID, const std::string &sName, const unsigned char unit, const unsigned char cType, const unsigned char cSubType, const std::string &sValue) {
	return CheckAndHandleNotification(DevRowIdx, HardwareID, ID, sName, unit, cType, cSubType, 0, sValue, static_cast<float>(atof(sValue.c_str())));
}

bool CNotificationHelper::CheckAndHandleNotification(const uint64_t DevRowIdx, const int HardwareID, const std::string &ID, const std::string &sName, const unsigned char unit, const unsigned char cType, const unsigned char cSubType, const int nValue, const std::string &sValue) {
	return CheckAndHandleNotification(DevRowIdx, HardwareID, ID, sName, unit, cType, cSubType, nValue, sValue, static_cast<float>(atof(sValue.c_str())));
}

bool CNotificationHelper::CheckAndHandleNotification(const uint64_t DevRowIdx, const int HardwareID, const std::string &ID, const std::string &sName, const unsigned char unit, const unsigned char cType, const unsigned char cSubType, const int nValue, const std::string &sValue, const float fValue) {
	float fValue2;
	bool r1, r2, r3;
	int nsize;
	int nexpected = 0;

	// Don't send notification for devices not in db
	// Notifications for switches are handled by CheckAndHandleSwitchNotification in UpdateValue() of SQLHelper
	if ((DevRowIdx == -1) || IsLightOrSwitch(cType, cSubType)) {
		return false;
	}

	int meterType = 0;
	std::vector<std::string> strarray;
	StringSplit(sValue, ";", strarray);
	nsize = strarray.size();
	switch(cType) {
		case pTypeP1Power:
			nexpected = 5;
			if (nsize >= nexpected) {
				return CheckAndHandleNotification(DevRowIdx, sName, cType, cSubType, NTYPE_USAGE, (float)atof(strarray[4].c_str()));
			}
			break;
		case pTypeRFXSensor:
			switch(cSubType) {
				case sTypeRFXSensorTemp:
					return CheckAndHandleTempHumidityNotification(DevRowIdx, sName, fValue, 0, true, false);
				case sTypeRFXSensorAD:
				case sTypeRFXSensorVolt:
					return CheckAndHandleNotification(DevRowIdx, sName, cType, cSubType, NTYPE_USAGE, fValue);
				default:
					break;
			}
			break;
		case pTypeSetpoint:
			switch(cSubType) {
				case sTypeSetpoint:
					return CheckAndHandleNotification(DevRowIdx, sName, cType, cSubType, NTYPE_SETPOINT, fValue);
				default:
					break;
			}
			break;
		case pTypeTEMP:
			return CheckAndHandleTempHumidityNotification(DevRowIdx, sName, fValue, 0, true, false);
		case pTypeHUM:
			return CheckAndHandleTempHumidityNotification(DevRowIdx, sName, 0.0, nValue, false, true);
		case pTypeTEMP_HUM:
			nexpected = 2;
			if (nsize >= nexpected) {
				float Temp = (float)atof(strarray[0].c_str());
				int Hum = atoi(strarray[1].c_str());
				float dewpoint = (float)CalculateDewPoint(Temp, Hum);
				r1 = CheckAndHandleTempHumidityNotification(DevRowIdx, sName, Temp, Hum, true, true);
				r2 = CheckAndHandleDewPointNotification(DevRowIdx, sName, Temp, dewpoint);
				return r1 && r2;
			}
			break;
		case pTypeTEMP_HUM_BARO:
			nexpected = 4;
			if (nsize >= nexpected) {
				float Temp = (float)atof(strarray[0].c_str());
				int Hum = atoi(strarray[1].c_str());
				float dewpoint = (float)CalculateDewPoint(Temp, Hum);
				r1 = CheckAndHandleTempHumidityNotification(DevRowIdx, sName, Temp, Hum, true, true);
				r2 = CheckAndHandleDewPointNotification(DevRowIdx, sName, Temp, dewpoint);
				r3 = CheckAndHandleNotification(DevRowIdx, sName, cType, cSubType, NTYPE_BARO, (float)atof(strarray[3].c_str()));
				return r1 && r2 && r3;
			}
			break;
		case pTypeRAIN:
			nexpected = 2;
			if (nsize >= nexpected) {
				fValue2 = (float)atof(strarray[1].c_str());
				return CheckAndHandleRainNotification(DevRowIdx, sName, cType, cSubType, NTYPE_RAIN, fValue2);
			}
			break;
		case pTypeTEMP_BARO:
			nexpected = 2;
			if (nsize >= nexpected) {
				float Temp = (float)atof(strarray[0].c_str());
				float Baro = (float)atof(strarray[1].c_str());
				r1 = CheckAndHandleTempHumidityNotification(DevRowIdx, sName, Temp, 0, true, false);
				r2 = CheckAndHandleNotification(DevRowIdx, sName, cType, cSubType, NTYPE_BARO, Baro);
				return r1 && r2;
			}
			break;
		case pTypeUV:
			nexpected = 2;
			if (nsize >= nexpected) {
				float Level = (float)atof(strarray[0].c_str());
				float Temp = (float)atof(strarray[1].c_str());
				if (cSubType == sTypeUV3)
				{
					r1 = CheckAndHandleTempHumidityNotification(DevRowIdx, sName, Temp, 0, true, false);
				}
				else
					r1 = true;
				r2 = CheckAndHandleNotification(DevRowIdx, sName, cType, cSubType, NTYPE_UV, Level);
				return r1 && r2;
			}
			break;
		case pTypeCURRENT:
			nexpected = 3;
			if (nsize >= nexpected) {
				float CurrentChannel1 = (float)atof(strarray[0].c_str());
				float CurrentChannel2 = (float)atof(strarray[1].c_str());
				float CurrentChannel3 = (float)atof(strarray[2].c_str());
				return CheckAndHandleAmpere123Notification(DevRowIdx, sName, CurrentChannel1, CurrentChannel2, CurrentChannel3);
			}
			break;
		case pTypeCURRENTENERGY:
			nexpected = 3;
			if (nsize >= nexpected) {
				float CurrentChannel1 = (float)atof(strarray[0].c_str());
				float CurrentChannel2 = (float)atof(strarray[1].c_str());
				float CurrentChannel3 = (float)atof(strarray[2].c_str());
				return CheckAndHandleAmpere123Notification(DevRowIdx, sName, CurrentChannel1, CurrentChannel2, CurrentChannel3);
			}
			break;
		case pTypeWIND:
			nexpected = 5;
			if (nsize >= nexpected) {
				float wspeedms = (float)(atof(strarray[2].c_str()) / 10.0F);
				float temp = (float)atof(strarray[4].c_str());
				r1 = CheckAndHandleNotification(DevRowIdx, sName, cType, cSubType, NTYPE_WIND, wspeedms);
				r2 = CheckAndHandleTempHumidityNotification(DevRowIdx, sName, temp, 0, true, false);
				return r1 && r2;
			}
			break;
		case pTypeYouLess:
			nexpected = 2;
			if (nsize >= nexpected) {
				float usagecurrent = (float)atof(strarray[1].c_str());
				return CheckAndHandleNotification(DevRowIdx, sName, cType, cSubType, NTYPE_USAGE, usagecurrent);
			}
			break;
		case pTypeAirQuality:
			return CheckAndHandleNotification(DevRowIdx, sName, cType, cSubType, NTYPE_USAGE, (float)nValue);
		case pTypeWEIGHT:
		case pTypeLux:
			return CheckAndHandleNotification(DevRowIdx, sName, cType, cSubType, NTYPE_USAGE, fValue);
		case pTypeRego6XXTemp:
			return CheckAndHandleNotification(DevRowIdx, sName, cType, cSubType, NTYPE_TEMPERATURE, fValue);
		case pTypePOWER:
			nexpected = 1;
			if (nsize >= nexpected) {
				fValue2 = (float)atof(strarray[0].c_str());
				return CheckAndHandleNotification(DevRowIdx, sName, cType, cSubType, NTYPE_USAGE, fValue2);
			}
			break;
		case pTypeRFXMeter:
			switch(cSubType) {
				case sTypeRFXMeterCount:
					return CheckAndHandleNotification(DevRowIdx, sName, cType, cSubType, NTYPE_TODAYCOUNTER, fValue);
				default:
					break;
			}
			break;
		case pTypeUsage:
			return CheckAndHandleNotification(DevRowIdx, sName, cType, cSubType, NTYPE_USAGE, fValue);
			break;
		case pTypeP1Gas:
			// ignore, notification is done day by day in SQLHelper
			return false;
		case pTypeGeneral:
			switch(cSubType) {
				case sTypeVisibility:
					m_sql.GetMeterType(HardwareID, ID.c_str(), unit, cType, cSubType, meterType);
					fValue2 = fValue;
					if (meterType == 1) {
						//miles
						fValue2 *= 0.6214F;
					}
					return CheckAndHandleNotification(DevRowIdx, sName, cType, cSubType, NTYPE_USAGE, fValue2);
				case sTypeDistance:
					m_sql.GetMeterType(HardwareID, ID.c_str(), unit, cType, cSubType, meterType);
					fValue2 = fValue;
					if (meterType == 1) {
						//inches
						fValue2 *= 0.3937007874015748F;
					}
					return CheckAndHandleNotification(DevRowIdx, sName, cType, cSubType, NTYPE_USAGE, fValue2);
				case sTypeBaro:
				case sTypeKwh:
					nexpected = 1;
					if (nsize >= nexpected) {
						fValue2 = (float)atof(strarray[0].c_str());
						return CheckAndHandleNotification(DevRowIdx, sName, cType, cSubType, NTYPE_USAGE, fValue2);
					}
					break;
				case sTypePercentage:
					return CheckAndHandleNotification(DevRowIdx, sName, cType, cSubType, NTYPE_PERCENTAGE, fValue);
				case sTypeSoilMoisture:
				case sTypeLeafWetness:
					return CheckAndHandleNotification(DevRowIdx, sName, cType, cSubType, NTYPE_USAGE, (float)nValue);
				case sTypeFan:
				case sTypeSoundLevel:
				case sTypeSolarRadiation:
				case sTypeVoltage:
				case sTypeCurrent:
				case sTypePressure:
				case sTypeWaterflow:
				case sTypeCustom:
					return CheckAndHandleNotification(DevRowIdx, sName, cType, cSubType, NTYPE_USAGE, fValue);
				case sTypeAlert:
					return CheckAndHandleAlertNotification(DevRowIdx, sName, sValue);
				default:
					// silently ignore other general devices
					return false;
			}
			break;
		case pTypeEvohome:
		case pTypeEvohomeRelay:
		case pTypeEvohomeWater:
		case pTypeEvohomeZone:
			//not handled
			return false;
			break;
		default:
			break;
	}

	std::string hName;
	CDomoticzHardwareBase *pHardware = m_mainworker.GetHardware(HardwareID);
	if (pHardware == nullptr)
	{
		hName = "";
	}
	else
	{
		hName = pHardware->m_Name;
	}

	if (nexpected > 0) {
		_log.Log(LOG_STATUS, "Warning: Expecting svalue with at least %d elements separated by semicolon, %d elements received (\"%s\"), notification not sent (Hardware: %d - %s, ID: %s, Unit: %d, Type: %02X - %s, SubType: %d - %s)", nexpected, nsize, sValue.c_str(), HardwareID, hName.c_str(), ID.c_str(), unit, cType, RFX_Type_Desc(cType, 1), cSubType, RFX_Type_SubType_Desc(cType, cSubType));
	}
	else {
		_log.Log(LOG_STATUS, "Warning: Notification NOT handled (Hardware: %d - %s, ID: %s, Unit: %d, Type: %02X - %s, SubType: %d - %s), please report on GitHub!", HardwareID, hName.c_str(), ID.c_str(), unit, cType, RFX_Type_Desc(cType, 1), cSubType, RFX_Type_SubType_Desc(cType, cSubType));
	}

	return false;
}

bool CNotificationHelper::CheckAndHandleTempHumidityNotification(
	const uint64_t Idx,
	const std::string &devicename,
	const float temp,
	const int humidity,
	const bool bHaveTemp,
	const bool bHaveHumidity)
{
	std::vector<_tNotification> notifications = GetNotifications(Idx);
	if (notifications.empty())
		return false;

	char szTmp[600];
	std::string notValue;

	std::string szExtraData = "|Name=" + devicename + "|";

	time_t atime = mytime(nullptr);

	//check if not sent 12 hours ago, and if applicable

	atime -= m_NotificationSensorInterval;

	std::string msg;

	std::string label = Notification_Type_Label(NTYPE_TEMPERATURE);
	std::string signtemp = Notification_Type_Desc(NTYPE_TEMPERATURE, 1);
	std::string signhum = Notification_Type_Desc(NTYPE_HUMIDITY, 1);

	for (const auto &n : notifications)
	{
		if (n.LastUpdate)
			TouchLastUpdate(n.ID);

		if ((atime >= n.LastSend) || (n.SendAlways) || (!n.CustomMessage.empty())) // emergency always goes true
		{
			std::string recoverymsg;
			bool bRecoveryMessage = false;
			bRecoveryMessage = CustomRecoveryMessage(n.ID, recoverymsg, true);
			if ((atime < n.LastSend) && (!n.SendAlways) && (!bRecoveryMessage))
				continue;
			std::vector<std::string> splitresults;
			StringSplit(n.Params, ";", splitresults);
			if (splitresults.size() < 3)
				continue; //impossible
			std::string ntype = splitresults[0];
			std::string custommsg;
			float svalue = static_cast<float>(atof(splitresults[2].c_str()));
			bool bSendNotification = false;
			bool bCustomMessage = false;
			bCustomMessage = CustomRecoveryMessage(n.ID, custommsg, false);

			if ((ntype == signtemp) && (bHaveTemp))
			{
				//temperature
				if (m_sql.m_tempunit == TEMPUNIT_F)
				{
					//Convert to Celsius
					svalue = static_cast<float>(ConvertToCelsius(svalue));
				}

				if (temp > 30.0) szExtraData += "Image=temp-gt-30|";
				else if (temp > 25.0) szExtraData += "Image=temp-25-30|";
				else if (temp > 20.0) szExtraData += "Image=temp-20-25|";
				else if (temp > 15.0) szExtraData += "Image=temp-15-20|";
				else if (temp > 10.0) szExtraData += "Image=temp-10-15|";
				else if (temp > 5.0) szExtraData += "Image=temp-5-10|";
				else szExtraData += "Image=temp48|";
				bSendNotification = ApplyRule(splitresults[1], (temp == svalue), (temp < svalue));
				if (bSendNotification && (!bRecoveryMessage || n.SendAlways))
				{
					sprintf(szTmp, "%s Temperature is %.1f %s [%s %.1f %s]", devicename.c_str(), temp, label.c_str(), splitresults[1].c_str(), svalue, label.c_str());
					msg = szTmp;
					sprintf(szTmp, "%.1f", temp);
					notValue = szTmp;
				}
				else if (!bSendNotification && bRecoveryMessage)
				{
					bSendNotification = true;
					msg = recoverymsg;
					std::string clearstr = "!";
					CustomRecoveryMessage(n.ID, clearstr, true);
				}
				else
				{
					bSendNotification = false;
				}
			}
			else if ((ntype == signhum) && (bHaveHumidity))
			{
				//humidity
				szExtraData += "Image=moisture48|";
				bSendNotification = ApplyRule(splitresults[1], (humidity == svalue), (humidity < svalue));
				if (bSendNotification && (!bRecoveryMessage || n.SendAlways))
				{
					sprintf(szTmp, "%s Humidity is %d %% [%s %.0f %%]", devicename.c_str(), humidity, splitresults[1].c_str(), svalue);
					msg = szTmp;
					sprintf(szTmp, "%d", humidity);
					notValue = szTmp;
				}
				else if (!bSendNotification && bRecoveryMessage)
				{
					bSendNotification = true;
					msg = recoverymsg;
					std::string clearstr = "!";
					CustomRecoveryMessage(n.ID, clearstr, true);
				}
				else
				{
					bSendNotification = false;
				}
			}
			if (bSendNotification)
			{
				if (bCustomMessage && !bRecoveryMessage)
					msg = ParseCustomMessage(custommsg, devicename, notValue);
				SendMessageEx(Idx, devicename, n.ActiveSystems, n.CustomAction, msg, msg, szExtraData, n.Priority, std::string(""), true);
				if (!bRecoveryMessage)
				{
					TouchNotification(n.ID);
					CustomRecoveryMessage(n.ID, msg, true);
				}
			}
		}
	}
	return true;
}

bool CNotificationHelper::CheckAndHandleDewPointNotification(
	const uint64_t Idx,
	const std::string &devicename,
	const float temp,
	const float dewpoint)
{
	std::vector<_tNotification> notifications = GetNotifications(Idx);
	if (notifications.empty())
		return false;

	char szTmp[600];
	std::string szExtraData = "|Name=" + devicename + "|Image=temp-0-5|";
	std::string notValue;

	time_t atime = mytime(nullptr);

	//check if not sent 12 hours ago, and if applicable

	atime -= m_NotificationSensorInterval;

	std::string msg;

	std::string signdewpoint = Notification_Type_Desc(NTYPE_DEWPOINT, 1);

	for (const auto &n : notifications)
	{
		if (n.LastUpdate)
			TouchLastUpdate(n.ID);
		if ((atime >= n.LastSend) || (n.SendAlways)) // emergency always goes true
		{
			std::vector<std::string> splitresults;
			StringSplit(n.Params, ";", splitresults);
			if (splitresults.empty())
				continue; //impossible
			std::string ntype = splitresults[0];

			if (ntype == signdewpoint)
			{
				//dewpoint
				if (temp <= dewpoint)
				{
					sprintf(szTmp, "%s Dew Point reached (%.1f degrees)", devicename.c_str(), temp);
					msg = szTmp;
					sprintf(szTmp, "%.1f", temp);
					notValue = szTmp;
					if (!n.CustomMessage.empty())
						msg = ParseCustomMessage(n.CustomMessage, devicename, notValue);
					SendMessageEx(Idx, devicename, n.ActiveSystems, n.CustomAction, msg, msg, szExtraData, n.Priority, std::string(""),
						      true);
					TouchNotification(n.ID);
				}
			}
		}
	}
	return true;
}

bool CNotificationHelper::CheckAndHandleValueNotification(
	const uint64_t Idx,
	const std::string &DeviceName,
	const int value)
{
	std::vector<_tNotification> notifications = GetNotifications(Idx);
	if (notifications.empty())
		return false;

	char szTmp[600];
	std::string szExtraData = "|Name=" + DeviceName + "|";

	time_t atime = mytime(nullptr);

	//check if not sent 12 hours ago, and if applicable
	atime -= m_NotificationSensorInterval;

	std::string msg;
	std::string notValue;

	std::string signvalue = Notification_Type_Desc(NTYPE_VALUE, 1);

	for (const auto &n : notifications)
	{
		if (n.LastUpdate)
			TouchLastUpdate(n.ID);
		if ((atime >= n.LastSend) || (n.SendAlways)) // emergency always goes true
		{
			std::vector<std::string> splitresults;
			StringSplit(n.Params, ";", splitresults);
			if (splitresults.size() < 2)
				continue; //impossible
			std::string ntype = splitresults[0];
			int svalue = static_cast<int>(atoi(splitresults[1].c_str()));

			if (ntype == signvalue)
			{
				if (value > svalue)
				{
					sprintf(szTmp, "%s is %d", DeviceName.c_str(), value);
					msg = szTmp;
					sprintf(szTmp, "%d", value);
					notValue = szTmp;
					if (!n.CustomMessage.empty())
						msg = ParseCustomMessage(n.CustomMessage, DeviceName, notValue);
					SendMessageEx(Idx, DeviceName, n.ActiveSystems, n.CustomAction, msg, msg, szExtraData, n.Priority, std::string(""),
						      true);
					TouchNotification(n.ID);
				}
			}
		}
	}
	return true;
}

bool CNotificationHelper::CheckAndHandleAmpere123Notification(
	const uint64_t Idx,
	const std::string &devicename,
	const float Ampere1,
	const float Ampere2,
	const float Ampere3)
{
	std::vector<_tNotification> notifications = GetNotifications(Idx);
	if (notifications.empty())
		return false;

	char szTmp[600];

	std::string szExtraData = "|Name=" + devicename + "|Image=current48|";

	time_t atime = mytime(nullptr);

	//check if not sent 12 hours ago, and if applicable
	atime -= m_NotificationSensorInterval;

	std::string msg;

	std::string notValue;

	std::string signamp1 = Notification_Type_Desc(NTYPE_AMPERE1, 1);
	std::string signamp2 = Notification_Type_Desc(NTYPE_AMPERE2, 1);
	std::string signamp3 = Notification_Type_Desc(NTYPE_AMPERE3, 1);

	for (const auto &n : notifications)
	{
		if (n.LastUpdate)
			TouchLastUpdate(n.ID);

		if ((atime >= n.LastSend) || (n.SendAlways) || (!n.CustomMessage.empty())) // emergency always goes true
		{
			std::string recoverymsg;
			bool bRecoveryMessage = false;
			bRecoveryMessage = CustomRecoveryMessage(n.ID, recoverymsg, true);
			if ((atime < n.LastSend) && (!n.SendAlways) && (!bRecoveryMessage))
				continue;
			std::vector<std::string> splitresults;
			StringSplit(n.Params, ";", splitresults);
			if (splitresults.size() < 3)
				continue; //impossible
			std::string ntype = splitresults[0];
			std::string custommsg;
			std::string ltype;
			float svalue = static_cast<float>(atof(splitresults[2].c_str()));
			float ampere = 0.0F;
			bool bSendNotification = false;
			bool bCustomMessage = false;
			bCustomMessage = CustomRecoveryMessage(n.ID, custommsg, false);

			if (ntype == signamp1)
			{
				ampere = Ampere1;
				ltype = Notification_Type_Desc(NTYPE_AMPERE1, 0);
			}
			else if (ntype == signamp2)
			{
				ampere = Ampere2;
				ltype = Notification_Type_Desc(NTYPE_AMPERE2, 0);
			}
			else if (ntype == signamp3)
			{
				ampere = Ampere3;
				ltype = Notification_Type_Desc(NTYPE_AMPERE3, 0);
			}
			bSendNotification = ApplyRule(splitresults[1], (ampere == svalue), (ampere < svalue));
			if (bSendNotification && (!bRecoveryMessage || n.SendAlways))
			{
				sprintf(szTmp, "%s %s is %.1f Ampere [%s %.1f Ampere]", devicename.c_str(), ltype.c_str(), ampere, splitresults[1].c_str(), svalue);
				msg = szTmp;
				sprintf(szTmp, "%.1f", ampere);
				notValue = szTmp;
			}
			else if (!bSendNotification && bRecoveryMessage)
			{
				bSendNotification = true;
				msg = recoverymsg;
				std::string clearstr = "!";
				CustomRecoveryMessage(n.ID, clearstr, true);
			}
			else
			{
				bSendNotification = false;
			}
			if (bSendNotification)
			{
				if (bCustomMessage && !bRecoveryMessage)
					msg = ParseCustomMessage(custommsg, devicename, notValue);
				SendMessageEx(Idx, devicename, n.ActiveSystems, n.CustomAction, msg, msg, szExtraData, n.Priority, std::string(""), true);
				if (!bRecoveryMessage)
				{
					TouchNotification(n.ID);
					CustomRecoveryMessage(n.ID, msg, true);
				}
			}
		}
	}
	return true;
}

bool CNotificationHelper::CheckAndHandleNotification(
	const uint64_t Idx,
	const std::string &devicename,
	const _eNotificationTypes ntype,
	const std::string &message)
{
	std::vector<_tNotification> notifications = GetNotifications(Idx);
	if (notifications.empty())
		return false;

	std::vector<std::vector<std::string> > result;
	result = m_sql.safe_query("SELECT SwitchType, CustomImage FROM DeviceStatus WHERE (ID=%" PRIu64 ")", Idx);
	if (result.empty())
		return false;

	std::string szExtraData = "|Name=" + devicename + "|SwitchType=" + result[0][0] + "|CustomImage=" + result[0][1] + "|";
	std::string notValue;

	time_t atime = mytime(nullptr);

	//check if not sent 12 hours ago, and if applicable
	atime -= m_NotificationSensorInterval;

	std::string ltype = Notification_Type_Desc(ntype, 1);
	for (const auto &n : notifications)
	{
		if (n.LastUpdate)
			TouchLastUpdate(n.ID);
		std::vector<std::string> splitresults;
		StringSplit(n.Params, ";", splitresults);
		if (splitresults.empty())
			continue; //impossible
		std::string atype = splitresults[0];
		if (atype == ltype)
		{
			if ((atime >= n.LastSend) || (n.SendAlways)) // emergency always goes true
			{
				std::string msg = message;
				if (!n.CustomMessage.empty())
					msg = ParseCustomMessage(n.CustomMessage, devicename, notValue);
				SendMessageEx(Idx, devicename, n.ActiveSystems, n.CustomAction, msg, msg, szExtraData, n.Priority, std::string(""), true);
				TouchNotification(n.ID);
			}
		}
	}
	return true;
}

bool CNotificationHelper::CheckAndHandleNotification(
	const uint64_t Idx,
	const std::string &devicename,
	const unsigned char devType,
	const unsigned char subType,
	const _eNotificationTypes ntype,
	const float mvalue)
{
	std::vector<_tNotification> notifications = GetNotifications(Idx);
	if (notifications.empty())
		return false;

	char szTmp[600];

	double intpart;
	std::string pvalue;
	if (modf(mvalue, &intpart) == 0)
		sprintf(szTmp, "%.0f", mvalue);
	else
		sprintf(szTmp, "%.1f", mvalue);
	pvalue = szTmp;

	std::vector<std::vector<std::string> > result;
	result = m_sql.safe_query("SELECT SwitchType FROM DeviceStatus WHERE (ID=%" PRIu64 ")", Idx);
	if (result.empty())
		return false;
	std::string szExtraData = "|Name=" + devicename + "|SwitchType=" + result[0][0] + "|";

	time_t atime = mytime(nullptr);

	//check if not sent 12 hours ago, and if applicable
	atime -= m_NotificationSensorInterval;

	std::string msg;

	std::string ltype = Notification_Type_Desc(ntype, 0);
	std::string nsign = Notification_Type_Desc(ntype, 1);
	std::string label = Notification_Type_Label(ntype);

	for (const auto &n : notifications)
	{
		if (n.LastUpdate)
			TouchLastUpdate(n.ID);

		if ((atime >= n.LastSend) || (n.SendAlways) || (!n.CustomMessage.empty())) // emergency always goes true
		{
			std::string recoverymsg;
			bool bRecoveryMessage = false;
			bRecoveryMessage = CustomRecoveryMessage(n.ID, recoverymsg, true);
			if ((atime < n.LastSend) && (!n.SendAlways) && (!bRecoveryMessage))
				continue;
			std::vector<std::string> splitresults;
			StringSplit(n.Params, ";", splitresults);
			if (splitresults.size() < 3)
				continue; //impossible
			std::string ntype = splitresults[0];
			std::string custommsg;
			float svalue = static_cast<float>(atof(splitresults[2].c_str()));
			bool bSendNotification = false;
			bool bCustomMessage = false;
			bCustomMessage = CustomRecoveryMessage(n.ID, custommsg, false);

			if (ntype == nsign)
			{
				bSendNotification = ApplyRule(splitresults[1], (mvalue == svalue), (mvalue < svalue));
				if (bSendNotification && (!bRecoveryMessage || n.SendAlways))
				{
					sprintf(szTmp, "%s %s is %s %s [%s %.1f %s]", devicename.c_str(), ltype.c_str(), pvalue.c_str(), label.c_str(), splitresults[1].c_str(), svalue, label.c_str());
					msg = szTmp;
				}
				else if (!bSendNotification && bRecoveryMessage)
				{
					bSendNotification = true;
					msg = recoverymsg;
					std::string clearstr = "!";
					CustomRecoveryMessage(n.ID, clearstr, true);
				}
				else
				{
					bSendNotification = false;
				}
			}
			if (bSendNotification)
			{
				if (bCustomMessage && !bRecoveryMessage)
					msg = ParseCustomMessage(custommsg, devicename, pvalue);
				SendMessageEx(Idx, devicename, n.ActiveSystems, n.CustomAction, msg, msg, szExtraData, n.Priority, std::string(""), true);
				if (!bRecoveryMessage)
				{
					TouchNotification(n.ID);
					CustomRecoveryMessage(n.ID, msg, true);
				}
			}
		}
	}
	return true;
}

bool CNotificationHelper::CheckAndHandleSwitchNotification(
	const uint64_t Idx,
	const std::string &devicename,
	const _eNotificationTypes ntype)
{
	std::vector<_tNotification> notifications = GetNotifications(Idx);
	if (notifications.empty())
		return false;

	std::vector<std::vector<std::string> > result;

	result = m_sql.safe_query("SELECT SwitchType, CustomImage FROM DeviceStatus WHERE (ID=%" PRIu64 ")",
		Idx);
	if (result.empty())
		return false;
	_eSwitchType switchtype = (_eSwitchType)atoi(result[0][0].c_str());
	std::string szExtraData = "|Name=" + devicename + "|SwitchType=" + result[0][0] + "|CustomImage=" + result[0][1] + "|";

	std::string msg;

	std::string ltype = Notification_Type_Desc(ntype, 1);

	time_t atime = mytime(nullptr);
	atime -= m_NotificationSwitchInterval;

	for (const auto &n : notifications)
	{
		if ((atime >= n.LastSend) || (n.SendAlways)) // emergency always goes true
		{
			std::vector<std::string> splitresults;
			StringSplit(n.Params, ";", splitresults);
			if (splitresults.empty())
				continue; //impossible
			std::string atype = splitresults[0];

			bool bSendNotification = false;
			std::string notValue;

			if (atype == ltype)
			{
				bSendNotification = true;
				msg = devicename;
				if (ntype == NTYPE_SWITCH_ON)
				{
					szExtraData += "Status=On|";
					switch (switchtype)
					{
					case STYPE_Doorbell:
						notValue = "pressed";
						break;
					case STYPE_Contact:
						notValue = "Open";
						szExtraData += "Image=Contact48_On|";
						break;
					case STYPE_DoorContact:
						notValue = "Open";
						szExtraData += "Image=Door48_On|";
						break;
					case STYPE_DoorLock:
						notValue = "Locked";
						szExtraData += "Image=Door48_Off|";
						break;
					case STYPE_DoorLockInverted:
						notValue = "Unlocked";
						szExtraData += "Image=Door48_On|";
						break;
					case STYPE_Motion:
						notValue = "movement detected";
						break;
					case STYPE_SMOKEDETECTOR:
						notValue = "ALARM/FIRE !";
						break;
					default:
						notValue = ">> ON";
						break;
					}
				}
				else {
					szExtraData += "Status=Off|";
					switch (switchtype)
					{
					case STYPE_DoorContact:
					case STYPE_Contact:
						notValue = "Closed";
						break;
					case STYPE_DoorLock:
						notValue = "Unlocked";
						szExtraData += "Image=Door48_On|";
						break;
					case STYPE_DoorLockInverted:
						notValue = "Locked";
						szExtraData += "Image=Door48_Off|";
						break;
					default:
						notValue = ">> OFF";
						break;
					}
				}
				msg += " " + notValue;
			}
			if (bSendNotification)
			{
				if (!n.CustomMessage.empty())
					msg = ParseCustomMessage(n.CustomMessage, devicename, notValue);
				SendMessageEx(Idx, devicename, n.ActiveSystems, n.CustomAction, msg, msg, szExtraData, n.Priority, std::string(""), true);
				TouchNotification(n.ID);
			}
		}
	}
	return true;
}

bool CNotificationHelper::CheckAndHandleSwitchNotification(
	const uint64_t Idx,
	const std::string &devicename,
	const _eNotificationTypes ntype,
	const int llevel)
{
	std::vector<_tNotification> notifications = GetNotifications(Idx);
	if (notifications.empty())
		return false;
	std::vector<std::vector<std::string> > result;

	result = m_sql.safe_query("SELECT SwitchType, CustomImage, Options FROM DeviceStatus WHERE (ID=%" PRIu64 ")",
		Idx);
	if (result.empty())
		return false;
	_eSwitchType switchtype = (_eSwitchType)atoi(result[0][0].c_str());
	std::string szExtraData = "|Name=" + devicename + "|SwitchType=" + result[0][0] + "|CustomImage=" + result[0][1] + "|";
	std::string sOptions = result[0][2];

	std::string msg;

	std::string ltype = Notification_Type_Desc(ntype, 1);

	time_t atime = mytime(nullptr);
	atime -= m_NotificationSwitchInterval;

	for (const auto &n : notifications)
	{
		if ((atime >= n.LastSend) || (n.SendAlways)) // emergency always goes true
		{
			std::vector<std::string> splitresults;
			StringSplit(n.Params, ";", splitresults);
			if (splitresults.empty())
				continue; //impossible
			std::string atype = splitresults[0];

			bool bSendNotification = false;
			std::string notValue;

			if (atype == ltype)
			{
				msg = devicename;
				if (ntype == NTYPE_SWITCH_ON)
				{
					if (splitresults.size() < 3)
						continue; //impossible
					bool bWhenEqual = (splitresults[1] == "=");
					int iLevel = atoi(splitresults[2].c_str());
					if (!bWhenEqual || iLevel < 10 || iLevel > 100)
						continue; //invalid

					if (llevel == iLevel)
					{
						bSendNotification = true;
						std::string sLevel = std::to_string(llevel);
						szExtraData += "Status=Level " + sLevel + "|";

						if (switchtype == STYPE_Selector)
						{
							std::map<std::string, std::string> options = m_sql.BuildDeviceOptions(sOptions);
							std::string levelNames = options["LevelNames"];
							std::vector<std::string> splitresults;
							StringSplit(levelNames, "|", splitresults);
							msg += " >> " + splitresults[(llevel / 10)];
							notValue = ">> " + splitresults[(llevel / 10)];
						}
						else
						{
							msg += " >> LEVEL " + sLevel;
							notValue = ">> LEVEL " + sLevel;
						}
					}
				}
				else
				{
					bSendNotification = true;
					szExtraData += "Status=Off|";
					msg += " >> OFF";
					notValue = ">> OFF";
				}
			}
			if (bSendNotification)
			{
				if (!n.CustomMessage.empty())
					msg = ParseCustomMessage(n.CustomMessage, devicename, notValue);
				SendMessageEx(Idx, devicename, n.ActiveSystems, n.CustomAction, msg, msg, szExtraData, n.Priority, std::string(""), true);
				TouchNotification(n.ID);
			}
		}
	}
	return true;
}

bool CNotificationHelper::CheckAndHandleRainNotification(
	const uint64_t Idx,
	const std::string &devicename,
	const unsigned char devType,
	const unsigned char subType,
	const _eNotificationTypes ntype,
	const float mvalue)
{
	std::vector<std::vector<std::string> > result;

	result = m_sql.safe_query("SELECT AddjValue,AddjMulti FROM DeviceStatus WHERE (ID=%" PRIu64 ")",
		Idx);
	if (result.empty())
		return false;
	//double AddjValue = atof(result[0][0].c_str());
	double AddjMulti = atof(result[0][1].c_str());

	char szDateEnd[40];

	time_t now = mytime(nullptr);
	struct tm tm1;
	localtime_r(&now, &tm1);
	struct tm ltime;
	ltime.tm_isdst = tm1.tm_isdst;
//GB3:	Use a midday hour to avoid a clash with possible DST jump
	ltime.tm_hour=14;
	ltime.tm_min = 0;
	ltime.tm_sec = 0;
	ltime.tm_year = tm1.tm_year;
	ltime.tm_mon = tm1.tm_mon;
	ltime.tm_mday = tm1.tm_mday;
	sprintf(szDateEnd, "%04d-%02d-%02d", ltime.tm_year + 1900, ltime.tm_mon + 1, ltime.tm_mday);

	if (subType == sTypeRAINWU || subType == sTypeRAINByRate)
	{
		//value is already total rain
		double total_real = mvalue;
		total_real *= AddjMulti;
		CheckAndHandleNotification(Idx, devicename, devType, subType, NTYPE_RAIN, (float)total_real);
	}
	else
	{
		result = m_sql.safe_query("SELECT MIN(Total) FROM Rain WHERE (DeviceRowID=%" PRIu64 " AND Date>='%q')",
			Idx, szDateEnd);
		if (!result.empty())
		{
			std::vector<std::string> sd = result[0];

			float total_min = static_cast<float>(atof(sd[0].c_str()));
			float total_max = mvalue;
			double total_real = total_max - total_min;
			total_real *= AddjMulti;
			CheckAndHandleNotification(Idx, devicename, devType, subType, NTYPE_RAIN, (float)total_real);
		}
	}
	return false;
}

bool CNotificationHelper::CheckAndHandleAlertNotification(
	const uint64_t Idx,
	const std::string &devicename,
	const std::string &sValue)
{
	std::vector<_tNotification> notifications = GetNotifications(Idx);
	if (notifications.empty())
		return false;

	time_t atime = mytime(nullptr);

	//check if not sent 12 hours ago, and if applicable
	atime -= m_NotificationSensorInterval;

	for (const auto &n : notifications)
	{
		if (n.LastUpdate)
			TouchLastUpdate(n.ID);
		std::vector<std::string> splitresults;
		StringSplit(n.Params, ";", splitresults);
		if (splitresults.empty())
			continue; //impossible
		std::string atype = splitresults[0];
		if ((atime >= n.LastSend) || (n.SendAlways)) // emergency always goes true
		{
			std::string msg = ParseCustomMessage(n.CustomMessage, devicename, sValue);
			SendMessageEx(Idx, devicename, n.ActiveSystems, n.CustomAction, msg, msg, std::string(""), n.Priority, std::string(""), true);
			TouchNotification(n.ID);
		}
	}
	return true;
}


void CNotificationHelper::CheckAndHandleLastUpdateNotification()
{
	if (m_notifications.empty())
		return;

	time_t atime = mytime(nullptr);
	atime -= m_NotificationSensorInterval;

	for (const auto &n : m_notifications)
	{
		for (const auto &n2 : n.second)
		{
			if (((atime >= n2.LastSend) || (n2.SendAlways) || (!n2.CustomMessage.empty()))
			    && (n2.LastUpdate)) // emergency always goes true
			{
				std::vector<std::string> splitresults;
				StringSplit(n2.Params, ";", splitresults);
				if (splitresults.size() < 3)
					continue;
				std::string ttype = Notification_Type_Desc(NTYPE_LASTUPDATE, 1);
				if (splitresults[0] == ttype)
				{
					std::string recoverymsg;
					bool bRecoveryMessage = false;
					bRecoveryMessage = CustomRecoveryMessage(n2.ID, recoverymsg, true);
					if ((atime < n2.LastSend) && (!n2.SendAlways) && (!bRecoveryMessage))
						continue;
					extern time_t m_StartTime;
					time_t btime = mytime(nullptr);
					std::string msg;
					std::string szExtraData;
					std::string custommsg;
					uint64_t Idx = n.first;
					uint32_t SensorTimeOut = static_cast<uint32_t>(atoi(splitresults[2].c_str()));  // minutes
					uint32_t diff = static_cast<uint32_t>(round(difftime(btime, n2.LastUpdate)));
					bool bStartTime = (difftime(btime, m_StartTime) < SensorTimeOut * 60);
					bool bSendNotification = ApplyRule(splitresults[1], (diff == SensorTimeOut * 60), (diff < SensorTimeOut * 60));
					bool bCustomMessage = false;
					bCustomMessage = CustomRecoveryMessage(n2.ID, custommsg, false);

					if (bSendNotification && !bStartTime && (!bRecoveryMessage || n2.SendAlways))
					{
						if (SystemUptime() < SensorTimeOut * 60 && (!bRecoveryMessage || n2.SendAlways))
							continue;
						std::vector<std::vector<std::string> > result;
						result = m_sql.safe_query("SELECT SwitchType FROM DeviceStatus WHERE (ID=%" PRIu64 ")", Idx);
						if (result.empty())
							continue;
						szExtraData = "|Name=" + n2.DeviceName + "|SwitchType=" + result[0][0] + "|";
						std::string ltype = Notification_Type_Desc(NTYPE_LASTUPDATE, 0);
						std::string label = Notification_Type_Label(NTYPE_LASTUPDATE);
						char szDate[50];
						char szTmp[300];
						struct tm ltime;
						localtime_r(&n2.LastUpdate, &ltime);
						sprintf(szDate, "%04d-%02d-%02d %02d:%02d:%02d", ltime.tm_year + 1900, ltime.tm_mon + 1, ltime.tm_mday,
							ltime.tm_hour, ltime.tm_min, ltime.tm_sec);
						sprintf(szTmp, "Sensor %s %s: %s [%s %d %s]", n2.DeviceName.c_str(), ltype.c_str(), szDate,
							splitresults[1].c_str(), SensorTimeOut, label.c_str());
						msg = szTmp;
					}
					else if (!bSendNotification && bRecoveryMessage)
					{
						msg = recoverymsg;
						std::string clearstr = "!";
						CustomRecoveryMessage(n2.ID, clearstr, true);
					}
					else
						continue;

					if (bCustomMessage && !bRecoveryMessage)
						msg = ParseCustomMessage(custommsg, n2.DeviceName, "");
					SendMessageEx(Idx, n2.DeviceName, n2.ActiveSystems, n2.CustomAction, msg, msg, szExtraData, n2.Priority,
						      std::string(""), true);
					if (!bRecoveryMessage)
					{
						TouchNotification(n2.ID);
						CustomRecoveryMessage(n2.ID, msg, true);
					}
				}
			}
		}
	}
}

void CNotificationHelper::TouchNotification(const uint64_t ID)
{
	char szDate[50];
	time_t atime = mytime(nullptr);
	struct tm ltime;
	localtime_r(&atime, &ltime);
	sprintf(szDate, "%04d-%02d-%02d %02d:%02d:%02d", ltime.tm_year + 1900, ltime.tm_mon + 1, ltime.tm_mday, ltime.tm_hour, ltime.tm_min, ltime.tm_sec);

	//Set LastSend date
	m_sql.safe_query("UPDATE Notifications SET LastSend='%q' WHERE (ID=%" PRIu64 ")",
		szDate, ID);

	//Also touch it internally
	std::lock_guard<std::mutex> l(m_mutex);

	for (auto &m : m_notifications)
	{
		for (auto &n : m.second)
		{
			if (n.ID == ID)
			{
				n.LastSend = atime;
				return;
			}
		}
	}
}

void CNotificationHelper::TouchLastUpdate(const uint64_t ID)
{
	time_t atime = mytime(nullptr);
	std::lock_guard<std::mutex> l(m_mutex);

	for (auto &m : m_notifications)
	{
		for (auto &n : m.second)
		{
			if (n.ID == ID)
			{
				n.LastUpdate = atime;
				return;
			}
		}
	}
}

bool CNotificationHelper::CustomRecoveryMessage(const uint64_t ID, std::string &msg, const bool isRecovery)
{
	std::lock_guard<std::mutex> l(m_mutex);

	for (auto &m : m_notifications)
	{
		for (auto &n : m.second)
		{
			if (n.ID == ID)
			{
				std::vector<std::string> splitresults;
				if (isRecovery)
				{
					StringSplit(n.Params, ";", splitresults);
					if (splitresults.size() < 4)
						return false;
					if (splitresults[3] != "1")
						return false;
				}

				std::string szTmp;
				StringSplit(n.CustomMessage, ";;", splitresults);
				if (msg.empty())
				{
					if (!splitresults.empty())
					{
						if (!splitresults[0].empty() && !isRecovery)
						{
							szTmp = splitresults[0];
							msg = szTmp;
							return true;
						}
						if (splitresults.size() > 1)
						{
							if (!splitresults[1].empty() && isRecovery)
							{
								szTmp = splitresults[1];
								msg = szTmp;
								return true;
							}
						}
					}
					return false;
				}
				if (!isRecovery)
					return false;

				if (!splitresults.empty())
				{
					if (!splitresults[0].empty())
						szTmp = splitresults[0];
				}
				if ((msg.find('!') != 0) && (msg.size() > 1))
				{
					szTmp.append(";;[Recovered] ");
					szTmp.append(msg);
				}
				std::vector<std::vector<std::string> > result;
				result = m_sql.safe_query("SELECT ID FROM Notifications WHERE (ID=='%" PRIu64 "') AND (Params=='%q')", n.ID,
							  n.Params.c_str());
				if (result.empty())
					return false;

				m_sql.safe_query("UPDATE Notifications SET CustomMessage='%q' WHERE ID=='%" PRIu64 "'", szTmp.c_str(),
						 n.ID);
				n.CustomMessage = szTmp;
				return true;
			}
		}
	}
	return false;
}

bool CNotificationHelper::AddNotification(
	const std::string &DevIdx,
	const bool Active,
	const std::string &Param,
	const std::string& CustomMessage,
	const std::string& CustomAction,
	const std::string &ActiveSystems,
	const int Priority,
	const bool SendAlways
	)
{
	std::vector<std::vector<std::string> > result;

	//First check for duplicate, because we do not want this
	result = m_sql.safe_query("SELECT ROWID FROM Notifications WHERE (DeviceRowID=='%q') AND (Params=='%q')",
		DevIdx.c_str(), Param.c_str());
	if (!result.empty())
		return false;//already there!

	int iSendAlways = (SendAlways == true) ? 1 : 0;
	m_sql.safe_query("INSERT INTO Notifications (DeviceRowID, Active, Params, CustomMessage, CustomAction, ActiveSystems, Priority, SendAlways) VALUES ('%q',%d,'%q','%q','%q','%q',%d,%d)",
		DevIdx.c_str(), (Active) ? 1 : 0, Param.c_str(), CustomMessage.c_str(), CustomAction.c_str(), ActiveSystems.c_str(), Priority, iSendAlways);
	ReloadNotifications();
	return true;
}

bool CNotificationHelper::RemoveDeviceNotifications(const std::string &DevIdx)
{
	m_sql.safe_query("DELETE FROM Notifications WHERE (DeviceRowID=='%q')",
		DevIdx.c_str());
	ReloadNotifications();
	return true;
}

bool CNotificationHelper::RemoveNotification(const std::string &ID)
{
	m_sql.safe_query("DELETE FROM Notifications WHERE (ID=='%q')",
		ID.c_str());
	ReloadNotifications();
	return true;
}

std::vector<_tNotification> CNotificationHelper::GetNotifications(const uint64_t DevIdx, const bool bActiveOnly)
{
	std::lock_guard<std::mutex> l(m_mutex);
	std::vector<_tNotification> ret;
	auto itt = m_notifications.find(DevIdx);
	if (itt != m_notifications.end())
	{
		if (!bActiveOnly)
			return itt->second;
		std::vector<_tNotification> tarray = itt->second;
		for (const auto& itt : tarray)
		{
			if (itt.Active)
				ret.push_back(itt);
		}
	}
	return ret;
}

bool CNotificationHelper::HasNotifications(const std::string &DevIdx)
{
	std::stringstream s_str(DevIdx);
	uint64_t idxll;
	s_str >> idxll;
	return HasNotifications(idxll);
}

bool CNotificationHelper::HasNotifications(const uint64_t DevIdx)
{
	std::lock_guard<std::mutex> l(m_mutex);
	return (m_notifications.find(DevIdx) != m_notifications.end());
}

//Re(Loads) all notifications stored in the database, so we do not have to query this all the time
void CNotificationHelper::ReloadNotifications()
{
	std::lock_guard<std::mutex> l(m_mutex);
	m_notifications.clear();
	std::vector<std::vector<std::string> > result;

	m_sql.GetPreferencesVar("NotificationSensorInterval", m_NotificationSensorInterval);
	m_sql.GetPreferencesVar("NotificationSwitchInterval", m_NotificationSwitchInterval);

	result = m_sql.safe_query("SELECT ID, DeviceRowID, Active, Params, CustomMessage, CustomAction, ActiveSystems, Priority, SendAlways, LastSend FROM Notifications ORDER BY DeviceRowID");
	if (result.empty())
		return;

	time_t mtime = mytime(nullptr);
	struct tm atime;
	localtime_r(&mtime, &atime);
	std::vector<std::string> splitresults;

	std::stringstream sstr;

	for (const auto &sd : result)
	{
		_tNotification notification;
		uint64_t Idx;

		sstr.clear();
		sstr.str("");

		sstr << sd[0];
		sstr >> notification.ID;

		sstr.clear();
		sstr.str("");

		sstr << sd[1];
		sstr >> Idx;

		notification.Active = atoi(sd[2].c_str()) != 0;
		notification.Params = sd[3];
		notification.CustomMessage = sd[4];
		notification.CustomAction = CURLEncode::URLDecode(sd[5]);
		notification.ActiveSystems = sd[6];
		notification.Priority = atoi(sd[7].c_str());
		notification.SendAlways = (atoi(sd[8].c_str())!=0);

		std::string stime = sd[9];
		if (stime == "0")
		{
			notification.LastSend = 0;
		}
		else
		{
			struct tm ntime;
			ParseSQLdatetime(notification.LastSend, ntime, stime, atime.tm_isdst);
		}
		std::string ttype = Notification_Type_Desc(NTYPE_LASTUPDATE, 1);
		StringSplit(notification.Params, ";", splitresults);
		if (splitresults[0] == ttype) {
			std::vector<std::vector<std::string> > result2;
			result2 = m_sql.safe_query(
				"SELECT B.Name, B.LastUpdate "
				"FROM Notifications AS A "
				"LEFT OUTER JOIN DeviceStatus AS B "
				"ON A.DeviceRowID=B.ID "
				"WHERE (A.Params LIKE '%q%%') "
				"AND (A.ID=='%" PRIu64 "') "
				"LIMIT 1",
				ttype.c_str(), notification.ID
				);
			if (result2.size() == 1) {
				struct tm ntime;
				notification.DeviceName = result2[0][0];
				std::string stime = result2[0][1];
				ParseSQLdatetime(notification.LastUpdate, ntime, stime, atime.tm_isdst);
			}
		}
		m_notifications[Idx].push_back(notification);
	}
}

//Webserver helpers
namespace http {
	namespace server {
		void CWebServer::Cmd_GetNotifications(WebEmSession& session, const request& req, Json::Value& root)
		{
			root["status"] = "OK";
			root["title"] = "getnotifications";

			int ii = 0;

			// Add known notification systems
			for (const auto& notifier : m_notifications.m_notifiers)
			{
				root["notifiers"][ii]["name"] = notifier.first;
				root["notifiers"][ii]["description"] = notifier.first;
				ii++;
			}

			uint64_t idx = 0;
			if (!request::findValue(&req, "idx").empty())
			{
				idx = std::stoull(request::findValue(&req, "idx"));
			}

			std::vector<_tNotification> notifications = m_notifications.GetNotifications(idx, false);
			if (!notifications.empty())
			{
				ii = 0;
				for (const auto& n : notifications)
				{
					root["result"][ii]["idx"] = Json::Value::UInt64(n.ID);
					root["result"][ii]["Active"] = (n.Active) ? "true" : "false";
					std::string sParams = n.Params;
					if (sParams.empty())
					{
						sParams = "S";
					}
					root["result"][ii]["Params"] = sParams;
					root["result"][ii]["Priority"] = n.Priority;
					root["result"][ii]["SendAlways"] = n.SendAlways;
					root["result"][ii]["CustomMessage"] = n.CustomMessage;
					root["result"][ii]["CustomAction"] = CURLEncode::URLEncode(n.CustomAction);
					root["result"][ii]["ActiveSystems"] = n.ActiveSystems;
					ii++;
				}
			}
		}
		void CWebServer::Cmd_AddNotification(WebEmSession& session, const request& req, Json::Value& root)
		{
			if (session.rights < 2)
			{
				session.reply_status = reply::forbidden;
				return; // Only admin user allowed
			}

			std::string idx = request::findValue(&req, "idx");
			if (idx.empty())
				return;

			std::string sactive = request::findValue(&req, "tactive");
			std::string stype = request::findValue(&req, "ttype");
			std::string swhen = request::findValue(&req, "twhen");
			std::string svalue = request::findValue(&req, "tvalue");
			std::string scustommessage = request::findValue(&req, "tmsg");
			std::string scustomaction = CURLEncode::URLDecode(request::findValue(&req, "taction"));
			std::string sactivesystems = request::findValue(&req, "tsystems");
			std::string spriority = request::findValue(&req, "tpriority");
			std::string ssendalways = request::findValue(&req, "tsendalways");
			std::string srecovery = (request::findValue(&req, "trecovery") == "true") ? "1" : "0";

			if (sactive.empty() || stype.empty() || swhen.empty() || svalue.empty() || spriority.empty() || ssendalways.empty() || srecovery.empty())
				return;

			std::string Param;

			_eNotificationTypes ntype = (_eNotificationTypes)atoi(stype.c_str());
			std::string ttype = Notification_Type_Desc(ntype, 1);
			if ((ntype == NTYPE_SWITCH_ON) || (ntype == NTYPE_SWITCH_OFF) || (ntype == NTYPE_DEWPOINT))
			{
				if ((ntype == NTYPE_SWITCH_ON) && (swhen == "2"))
				{ // '='
					unsigned char twhen = '=';
					Param = std_format("%s;%c;%s", ttype.c_str(), twhen, svalue.c_str());
				}
				else
					Param = ttype;
			}
			else
			{
				std::string twhen;
				if (swhen == "0")
					twhen = ">";
				else if (swhen == "1")
					twhen = ">=";
				else if (swhen == "2")
					twhen = "=";
				else if (swhen == "3")
					twhen = "!=";
				else if (swhen == "4")
					twhen = "<=";
				else
					twhen = "<";
				Param = std_format("%s;%s;%s;%s", ttype.c_str(), twhen.c_str(), svalue.c_str(), srecovery.c_str());
			}
			int priority = atoi(spriority.c_str());
			bool bOK = m_notifications.AddNotification(idx, (sactive == "true") ? true : false, Param, scustommessage, scustomaction, sactivesystems, priority, (ssendalways == "true") ? true : false);
			if (bOK)
			{
				root["status"] = "OK";
				root["title"] = "AddNotification";
			}
		}
		void CWebServer::Cmd_UpdateNotification(WebEmSession& session, const request& req, Json::Value& root)
		{
			if (session.rights < 2)
			{
				session.reply_status = reply::forbidden;
				return; // Only admin user allowed
			}

			std::string idx = request::findValue(&req, "idx");
			std::string devidx = request::findValue(&req, "devidx");
			if ((idx.empty()) || (devidx.empty()))
				return;

			std::string sactive = request::findValue(&req, "tactive");
			std::string stype = request::findValue(&req, "ttype");
			std::string swhen = request::findValue(&req, "twhen");
			std::string svalue = request::findValue(&req, "tvalue");
			std::string scustommessage = request::findValue(&req, "tmsg");
			std::string scustomaction = CURLEncode::URLDecode(request::findValue(&req, "taction"));
			std::string sactivesystems = request::findValue(&req, "tsystems");
			std::string spriority = request::findValue(&req, "tpriority");
			std::string ssendalways = request::findValue(&req, "tsendalways");
			std::string srecovery = (request::findValue(&req, "trecovery") == "true") ? "1" : "0";

			if (stype.empty() || sactive.empty() || swhen.empty() || svalue.empty() || spriority.empty() || ssendalways.empty() || srecovery.empty())
				return;
			root["status"] = "OK";
			root["title"] = "UpdateNotification";

			std::string recoverymsg;
			if ((srecovery == "1") && (m_notifications.CustomRecoveryMessage(strtoull(idx.c_str(), nullptr, 0), recoverymsg, true)))
			{
				scustommessage.append(";;");
				scustommessage.append(recoverymsg);
			}
			// delete old record
			m_notifications.RemoveNotification(idx);

			std::string Param;

			_eNotificationTypes ntype = (_eNotificationTypes)atoi(stype.c_str());
			std::string ttype = Notification_Type_Desc(ntype, 1);
			if ((ntype == NTYPE_SWITCH_ON) || (ntype == NTYPE_SWITCH_OFF) || (ntype == NTYPE_DEWPOINT))
			{
				if ((ntype == NTYPE_SWITCH_ON) && (swhen == "2"))
				{ // '='
					unsigned char twhen = '=';
					Param = std_format("%s;%c;%s", ttype.c_str(), twhen, svalue.c_str());
				}
				else
					Param = ttype;
			}
			else
			{
				std::string twhen;
				if (swhen == "0")
					twhen = ">";
				else if (swhen == "1")
					twhen = ">=";
				else if (swhen == "2")
					twhen = "=";
				else if (swhen == "3")
					twhen = "!=";
				else if (swhen == "4")
					twhen = "<=";
				else
					twhen = "<";
				Param = std_format("%s;%s;%s;%s", ttype.c_str(), twhen.c_str(), svalue.c_str(), srecovery.c_str());
			}
			int priority = atoi(spriority.c_str());
			m_notifications.AddNotification(devidx, (sactive == "true") ? true : false, Param, scustommessage, scustomaction, sactivesystems, priority, (ssendalways == "true") ? true : false);
		}
		void CWebServer::Cmd_DeleteNotification(WebEmSession& session, const request& req, Json::Value& root)
		{
			if (session.rights < 2)
			{
				session.reply_status = reply::forbidden;
				return; // Only admin user allowed
			}

			std::string idx = request::findValue(&req, "idx");
			if (idx.empty())
				return;

			root["status"] = "OK";
			root["title"] = "DeleteNotification";

			m_notifications.RemoveNotification(idx);
		}
	} // namespace server
} // namespace http
