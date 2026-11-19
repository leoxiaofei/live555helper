/* ---------------------------------------------------------------------------
** This software is in the public domain, furnished "as is", without technical
** support, and with no warranty, express or implied, as to its usefulness for
** any purpose.
**
** rtspconnectionclient.cpp
** 
** Interface to an RTSP client connection
** 
** -------------------------------------------------------------------------*/


#include "rtspconnectionclient.h"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <memory>
#include <sstream>

// declared below, used by the RTSPConnection constructor
static time_t parseClockTime(const std::string& clockTime);


RTSPConnection::RTSPConnection(Environment& env, Callback* callback, const char* rtspURL, const std::map<std::string,std::string> & opts, int verbosityLevel)
				: m_startCallbackTask(NULL)
				, m_stopCallbackTask(NULL)
				, m_PlayTask(NULL)
				, m_PauseTask(NULL)
				, m_env(env)
				, m_callback(callback)
				, m_url(rtspURL)
				, m_timeout(decodeTimeoutOption(opts))
				, m_rtptransport(decodeRTPTransport(opts))
				, m_verbosity(verbosityLevel)
				, m_rtspClient(NULL)
{
}

void RTSPConnection::start(unsigned int delay)
{
	m_env.taskScheduler().unscheduleDelayedTask(m_startCallbackTask);
	m_env.taskScheduler().unscheduleDelayedTask(m_stopCallbackTask);
	m_startCallbackTask = m_env.taskScheduler().scheduleDelayedTask(delay*1000000, TaskstartCallback, this);
}	

void RTSPConnection::TaskstartCallback() 
{		
	Medium::close(m_rtspClient);	
	m_rtspClient = new RTSPClientConnection(*this, m_env, m_callback, m_url.c_str(), m_timeout, m_rtptransport, m_verbosity);	
}

void RTSPConnection::stop()
{
	m_env.taskScheduler().unscheduleDelayedTask(m_startCallbackTask);
	m_env.taskScheduler().unscheduleDelayedTask(m_stopCallbackTask);
	m_stopCallbackTask = m_env.taskScheduler().scheduleDelayedTask(0, TaskstopCallback, this);
}

void RTSPConnection::TaskstopCallback() 
{		
	Medium::close(m_rtspClient);
	m_rtspClient = NULL;
}

RTSPConnection::~RTSPConnection()
{
	m_env.taskScheduler().unscheduleDelayedTask(m_startCallbackTask);
	m_env.taskScheduler().unscheduleDelayedTask(m_stopCallbackTask);
	m_env.taskScheduler().unscheduleDelayedTask(m_PlayTask);
	m_env.taskScheduler().unscheduleDelayedTask(m_PauseTask);
}

void RTSPConnection::scale(float scale)
{
	if(m_rtspClient)
	{
		m_rtspClient->setScale(scale);

		m_env.taskScheduler().unscheduleDelayedTask(m_PlayTask);
		m_PlayTask = m_env.taskScheduler().scheduleDelayedTask(0, TaskPlay, this);
	}
}

void RTSPConnection::seek(double nptStartTime)
{
	if(m_rtspClient)
	{
		m_rtspClient->setStartTime(std::string(), nptStartTime);
	
		m_env.taskScheduler().unscheduleDelayedTask(m_PlayTask);
		m_PlayTask = m_env.taskScheduler().scheduleDelayedTask(0, TaskPlay, this);
	}
}

void RTSPConnection::seek(const std::string& clockStartTime)
{
	if(m_rtspClient)
	{
		m_rtspClient->setStartTime(clockStartTime);

		m_env.taskScheduler().unscheduleDelayedTask(m_PlayTask);
		m_PlayTask = m_env.taskScheduler().scheduleDelayedTask(0, TaskPlay, this);
	}
}

void RTSPConnection::TaskPlay()
{
	if (m_rtspClient != NULL)
	{
		m_rtspClient->sendPlay();
	}
	else
	{
		m_env << "RTSPConnection cannot send PLAY command: client is not connected\n";
	}
}

void RTSPConnection::pause()
{
	if (m_rtspClient)
	{
		m_env.taskScheduler().unscheduleDelayedTask(m_PauseTask);
		m_PauseTask = m_env.taskScheduler().scheduleDelayedTask(0, TaskPause, this);
	}
}

void RTSPConnection::resume()
{
	if (m_rtspClient)
	{
		m_env.taskScheduler().unscheduleDelayedTask(m_PlayTask);
		m_PlayTask = m_env.taskScheduler().scheduleDelayedTask(0, TaskPlay, this);
	}
}

void RTSPConnection::TaskPause()
{
	if (m_rtspClient != NULL)
	{
		m_rtspClient->sendPause();
	}
	else
	{
		m_env << "RTSPConnection cannot send PAUSE command: client is not connected\n";
	}
}

double RTSPConnection::getNormalPlayTime(const char* id, struct timeval presentationTime)
{
	// the NPT is identical for all the tracks of the presentation, the track id is not used to compute it
	if (m_rtspClient != NULL)
	{
		return m_rtspClient->getNormalPlayTime(presentationTime);
	}
	return -1.0;
}

int getHttpTunnelPort(int  rtptransport, const char* rtspURL) 
{
	int httpTunnelPort = 0;
	if (rtptransport == RTSPConnection::RTPOVERHTTP) 
	{
		std::string url = rtspURL;
		const char * pattern = "://";
		std::size_t pos = url.find(pattern);
		if (pos != std::string::npos) {
			url.erase(0,pos+strlen(pattern));
		}
		pos = url.find_first_of("/");
		if (pos != std::string::npos) {
			url.erase(pos);
		}
		pos = url.find_first_of(":");
		if (pos != std::string::npos) {
			url.erase(0,pos+1);
			httpTunnelPort = std::stoi(url);
        }
	}
	return httpTunnelPort;
}
		
RTSPConnection::RTSPClientConnection::RTSPClientConnection(RTSPConnection& connection, Environment& env, Callback* callback, const char* rtspURL, int timeout, int  rtptransport, int verbosityLevel) 
				: RTSPClientConstrutor(env, rtspURL, verbosityLevel, NULL, getHttpTunnelPort(rtptransport, rtspURL))
				, m_ConnectionTimeoutTask(NULL)
				, m_DataArrivalTimeoutTask(NULL)
				, m_connection(connection)
				, m_timeout(timeout)
				, m_rtptransport(rtptransport)
				, m_session(NULL)
				, m_subSessionIter(NULL)
				, m_callback(callback)
				, m_nbPacket(0)
				, m_nptStartTime(0)
				, m_clockStartTime("")
				, m_subSession(NULL)
				, m_nptSubsession(NULL)
				, m_KeepAliveTask(NULL)
				, m_scale(1.0f)
				, m_getparameterSupported(false)
				, m_clockMode(false)
{
	// start tasks
	m_ConnectionTimeoutTask = envir().taskScheduler().scheduleDelayedTask(m_timeout*1000000, TaskConnectionTimeout, this);
	
	// initiate connection process
	this->sendOptionsCommand(continueAfterOPTIONS);
}

RTSPConnection::RTSPClientConnection::~RTSPClientConnection()
{
	envir().taskScheduler().unscheduleDelayedTask(m_ConnectionTimeoutTask);
	envir().taskScheduler().unscheduleDelayedTask(m_DataArrivalTimeoutTask);
    envir().taskScheduler().unscheduleDelayedTask(m_KeepAliveTask); 
	
	delete m_subSessionIter;
	
	// free subsession
	if (m_session != NULL) 
	{
		MediaSubsessionIterator iter(*m_session);
		MediaSubsession* subsession;
		while ((subsession = iter.next()) != NULL) 
		{
			if (subsession->sink) 
			{
				envir() << "Close session: " << subsession->mediumName() << "/" << subsession->codecName() << "\n";
				Medium::close(subsession->sink);
				subsession->sink = NULL;
			}
		}
		if (fVerbosityLevel > 1)
		{
			envir() << " Send Teardown command" << "\n";
		}
		this->sendTeardownCommand(*m_session, continueAfterTEARDOWN);
		Medium::close(m_session);
	}
}


std::string getParamValue(const std::string& url, const std::string& param) {
	std::string value;
    size_t pos = url.find(param + "=");
    if (pos != std::string::npos) {
        value = url.substr(pos + param.size() + 1);
    }
    return value;
}

/// <summary>
/// Get params npttime or starttime from a query string 
/// urlStr rtsp://192.168.31.124:554/camera02?npttime=74652
/// urlStr rtsp://192.168.31.124:554/camera02?starttime=20210129T103000Z
/// Be aware only the first params is sent to here
/// </summary>
void RTSPConnection::RTSPClientConnection::setNptstartTime()
{
	m_nptStartTime = 0;
	m_clockEndTime = "";
	m_scale = 1.0f;

	std::string urlStr = m_connection.getUrl();

	std::string nptTimeValue = getParamValue(urlStr, "ntptime");
	if (!nptTimeValue.empty()) {
		m_nptStartTime = std::stod(nptTimeValue);
	}

	std::string startTimeValue = getParamValue(urlStr, "starttime");
	if (!startTimeValue.empty()) {
		size_t pos = startTimeValue.find('&');
		if (pos != std::string::npos) {
			startTimeValue = startTimeValue.substr(0, pos);
		}
		m_clockStartTime = startTimeValue;
	}

	std::string endTimeValue = getParamValue(urlStr, "endtime");
	if (!endTimeValue.empty()) {
		size_t pos = endTimeValue.find('&');
		if (pos != std::string::npos) {
			endTimeValue = endTimeValue.substr(0, pos);
		}
		m_clockEndTime = endTimeValue;
	}

	std::string scaleValue = getParamValue(urlStr, "scale");
	if (!scaleValue.empty()) {
		m_scale = std::stof(scaleValue);
	}
}

void RTSPConnection::RTSPClientConnection::continueAfterOPTIONS(int resultCode, char* resultString)
{
    if (resultCode != 0) 
    {
        envir() << "Failed to send OPTIONS: " << resultString << "\n";
        m_callback->onError(m_connection, resultString);
    }
    else
    {
        if (fVerbosityLevel > 1) 
        {
            envir() << "OPTIONS OK: " << resultString << "\n";
        }

        std::string options(resultString);
        if (options.find("GET_PARAMETER") != std::string::npos) 
        {
            envir() << "GET_PARAMETER is supported by the server.\n";
			m_getparameterSupported = true;
		}
		else 
		{
			envir() << "GET_PARAMETER is NOT supported by the server.\n";
        }
				
        this->sendNextCommand();
    }
    delete[] resultString;
}

void RTSPConnection::RTSPClientConnection::setStartTime(const std::string& clockStartTime, double nptStartTime)
{
	m_clockStartTime = clockStartTime;
	m_nptStartTime = nptStartTime;
}

void RTSPConnection::RTSPClientConnection::setScale(float scale)
{
	m_scale = scale;
}

double RTSPConnection::RTSPClientConnection::getNormalPlayTime(struct timeval const& presentationTime)
{
	// the NPT is computed by live555 from the "RTP-Info:" header of the PLAY answer and
	// the RTCP synchronized presentation time, it is identical for all the tracks:
	// only the subsession selected when the PLAY answer was parsed is used
	if ( (m_nptSubsession != NULL)
	  && (m_nptSubsession->rtpInfo.infoIsNew || m_nptSubsession->rtpSource()->hasBeenSynchronizedUsingRTCP()) )
	{
		return m_nptSubsession->getNormalPlayTime(presentationTime);
	}
	return -1.0;
}

void RTSPConnection::RTSPClientConnection::sendPlay()
{
	if(m_session) {
		if (m_clockStartTime != "") {
			envir() << "send PLAY Range: clock=" << m_clockStartTime.c_str() << "-" << m_clockEndTime.c_str() << " scale:" << m_scale << "\n";
			this->sendPlayCommand(*m_session, continueAfterPLAY, m_clockStartTime.c_str(), m_clockEndTime.c_str(), m_scale);
			m_clockStartTime.clear();
		}
		else if (m_nptStartTime > 0) {
			envir() << "send PLAY Range: npt=" << m_nptStartTime << "- scale:" << m_scale << "\n";
			this->sendPlayCommand(*m_session, continueAfterPLAY, m_nptStartTime, -1.0, m_scale);
			m_nptStartTime = 0;
		}
		else {
			envir() << "send PLAY Range: (resume) scale:" << m_scale << "\n";
			this->sendPlayCommand(*m_session, continueAfterPLAY, -1.0, -1.0, m_scale);
		}
	}
}

std::vector<float> RTSPConnection::RTSPClientConnection::getSupportedScales() const
{
	// parse the "Scales=" list of the last "Media-Properties:" answer,
	// e.g. Scales="-1, 0.5, 0.25, 0.125,:1, 2, 4"
	std::vector<float> scales;
	const char* mediaProperties = this->mediaProperties();
	if (mediaProperties == NULL)
	{
		return scales;
	}
	const char* pos = strstr(mediaProperties, "Scales=");
	if (pos == NULL)
	{
		return scales;
	}
	pos += strlen("Scales=");
	if (*pos == '"')
	{
		// quoted list: Scales="-1, 0.5, 2, 4"
		++pos;
	}
	while ( (*pos != '\0') && (*pos != '"') && (scales.size() < 16) )
	{
		char* end = NULL;
		float scale = strtof(pos, &end);
		if (end != pos)
		{
			scales.push_back(scale);
			pos = end;
		}
		else
		{
			// skip an unparsable character (e.g. ':' in "0.125,:1")
			++pos;
		}
		while ( (*pos == ',') || (*pos == ' ') )
		{
			++pos;
		}
	}
	return scales;
}

std::vector<float> RTSPConnection::getSupportedScales() const
{
	if (m_rtspClient != NULL)
	{
		return m_rtspClient->getSupportedScales();
	}
	return std::vector<float>();
}

double RTSPConnection::RTSPClientConnection::getPlayDuration() const
{
	// duration of the play range: from the npt range of the SDP/PLAY answer,
	// or computed from the absolute end time when the npt end time is unknown
	// (an open ended "Range:" in the PLAY answer resets the npt end time)
	if (m_session == NULL)
	{
		return 0.0;
	}
	if (m_session->playEndTime() > m_session->playStartTime())
	{
		return m_session->playEndTime() - m_session->playStartTime();
	}
	if (m_session->absEndTime() != NULL)
	{
		// absolute end time of the range, the start is the npt start time
		// when the absolute start time is not known (it is the beginning of the range)
		struct tm tmEnd;
		memset(&tmEnd, 0, sizeof(tmEnd));
		if (sscanf(m_session->absEndTime(), "%04d%02d%02dT%02d%02d%02dZ", &tmEnd.tm_year, &tmEnd.tm_mon, &tmEnd.tm_mday, &tmEnd.tm_hour, &tmEnd.tm_min, &tmEnd.tm_sec) == 6)
		{
			tmEnd.tm_year -= 1900;
			tmEnd.tm_mon -= 1;
			time_t endTime = timegm(&tmEnd);

			time_t startTime = 0;
			const char* absStartTime = m_session->absStartTime();
			if (absStartTime != NULL)
			{
				struct tm tmStart;
				memset(&tmStart, 0, sizeof(tmStart));
				if (sscanf(absStartTime, "%04d%02d%02dT%02d%02d%02dZ", &tmStart.tm_year, &tmStart.tm_mon, &tmStart.tm_mday, &tmStart.tm_hour, &tmStart.tm_min, &tmStart.tm_sec) == 6)
				{
					tmStart.tm_year -= 1900;
					tmStart.tm_mon -= 1;
					startTime = timegm(&tmStart);
				}
			}
			return difftime(endTime, startTime);
		}
	}
	return 0.0;
}

void RTSPConnection::RTSPClientConnection::sendPause()
{
	if (m_session)
	{
		this->sendPauseCommand(*m_session, continueAfterPAUSE);
	}
}

void RTSPConnection::RTSPClientConnection::sendNextCommand()
{
	if (m_subSessionIter == NULL)
	{
		// no SDP, send DESCRIBE
		this->sendDescribeCommand(continueAfterDESCRIBE); 
	}
	else
	{
		m_subSession = m_subSessionIter->next();
		if (m_subSession != NULL) 
		{
			// still subsession to SETUP
			if (!m_subSession->initiate()) 
			{
				envir() << "Failed to initiate " << m_subSession->mediumName() << "/" << m_subSession->codecName() << " subsession: " << envir().getResultMsg() << "\n";
				this->sendNextCommand();
			}
			else 
			{
				if (fVerbosityLevel > 1)
				{				
					envir() << "Initiated " << m_subSession->mediumName() << "/" << m_subSession->codecName() << " subsession" << "\n";
				}
				this->sendSetupCommand(*m_subSession, continueAfterSETUP, false, (m_rtptransport == RTPOVERTCP), (m_rtptransport == RTPUDPMULTICAST) );
			}
		}
		else
		{
			// no more subsession to SETUP, send PLAY
			setNptstartTime();
			if (fVerbosityLevel > 1)
			{
				envir() << "****************************************************************************************\n";
				envir() << " nptTime is given read video from it :: m_nptStartTime " << m_nptStartTime << "\n";
				envir() << " clockstarttime is given read video from it :: m_clockStartTime " << m_clockStartTime.c_str() << "\n";
				envir() << " scale is given read video from it :: " << m_scale << "\n";
			}

			sendPlay();
		}
	}
}



void RTSPConnection::RTSPClientConnection::continueAfterDESCRIBE(int resultCode, char* resultString)
{
	if (resultCode != 0) 
	{
		envir() << "Failed to DESCRIBE: " << resultString << "\n";
		m_callback->onError(m_connection, resultString);
	}
	else
	{
		if (fVerbosityLevel > 1)
		{
			envir() << "Got SDP:\n" << resultString << "\n";
		}
		m_session = MediaSession::createNew(envir(), resultString);
		if (m_session)
		{
			m_subSessionIter = new MediaSubsessionIterator(*m_session);
			this->sendNextCommand();
		}
		else
		{
			if (fVerbosityLevel > 1)
			{
				envir() << "MediaSession::createNew() failed! (result string: " << resultString << ")\n";
			}
			m_callback->onError(m_connection, "MediaSession::createNew() failed!");
		}
	}
	delete[] resultString;
}

void RTSPConnection::RTSPClientConnection::continueAfterSETUP(int resultCode, char* resultString)
{
	if (resultCode != 0)
	{
		envir() << "Failed to SETUP: " << resultString << "\n";
		m_callback->onError(m_connection, resultString);
	}
	else
	{
		envir() << "Requested URL : " << m_connection.getUrl().c_str() << "\n";
		MediaSink* sink = SessionSink::createNew(envir(), m_callback);
		if (sink == NULL) 
		{
			envir() << "Failed to create sink for \"" << m_subSession->mediumName() << "/" << m_subSession->codecName() << "\" subsession error: " << envir().getResultMsg() << "\n";
			m_callback->onError(m_connection, envir().getResultMsg());			
		} 
		else if (m_callback->onNewSession(sink->name(), m_subSession->mediumName(), m_subSession->codecName(), m_subSession->savedSDPLines(), m_subSession->rtpTimestampFrequency(), m_subSession->numChannels())) 
		{
			envir() << "Start playing sink for \"" << m_subSession->mediumName() << "/" << m_subSession->codecName() << "\" subsession" << "\n";
			m_subSession->sink = sink;
			m_subSession->sink->startPlaying(*(m_subSession->readSource()), NULL, NULL);
		} 
		else 
		{
			Medium::close(sink);
		}
	}
	delete[] resultString;
	this->sendNextCommand();  
}	

void RTSPConnection::RTSPClientConnection::continueAfterPLAY(int resultCode, char* resultString)
{
	if (resultCode != 0) 
	{
		envir() << "Failed to PLAY: " << resultString << "\n";
		m_callback->onError(m_connection, resultString);
	}
	else
	{
		if (fVerbosityLevel > 1)
		{
			envir() << "PLAY OK" << "\n";
		}

			// the server may negotiate a different "Scale:" value in the PLAY response,
			// live555 already parsed it into MediaSession::scale() (left to 1.0 when the header is absent,
			// so 1.0 also means "no Scale header": most servers apply the requested speed without echoing it)
			float scale = m_session->scale();
			if (scale != 0.0f && scale != 1.0f && scale != m_scale)
			{
				envir() << "Server scale is now:" << scale << " (was " << m_scale << ")\n";
				m_scale = scale;
			}

		// select the subsession computing the normal play time: prefer the first track that
		// received a "RTP-Info:" entry in the PLAY answer (live555 assigns the entries in
		// subsession order, some servers send less entries than tracks), fallback to the
		// first with a RTP source; the NPT is identical for all the tracks
		m_nptSubsession = NULL;
		{
			MediaSubsessionIterator iter(*m_session);
			MediaSubsession* subsession;
			while ((subsession = iter.next()) != NULL)
			{
				if (subsession->rtpSource() == NULL)
				{
					continue;
				}
				if (m_nptSubsession == NULL)
				{
					// fallback: first subsession with a RTP source
					m_nptSubsession = subsession;
				}
				if (subsession->rtpInfo.infoIsNew)
				{
					// subsession that received a "RTP-Info:" entry
					m_nptSubsession = subsession;
					break;
				}
			}
		}

		m_clockMode = m_session->absStartTime() != NULL;

		// the session may be re-PLAYed (seek or scale change), unschedule tasks before scheduling new ones
		envir().taskScheduler().unscheduleDelayedTask(m_DataArrivalTimeoutTask);
		envir().taskScheduler().unscheduleDelayedTask(m_KeepAliveTask);
		m_DataArrivalTimeoutTask = envir().taskScheduler().scheduleDelayedTask(m_timeout*1000000, TaskDataArrivalTimeout, this);
		if (m_getparameterSupported) {
			m_KeepAliveTask = envir().taskScheduler().scheduleDelayedTask(m_timeout * 1000000, TaskKeepAlive, this);
		}

		m_callback->onPlayUpdate(m_connection);
	}
	envir().taskScheduler().unscheduleDelayedTask(m_ConnectionTimeoutTask);
	delete[] resultString;
}

void RTSPConnection::RTSPClientConnection::continueAfterPAUSE(int resultCode, char* resultString)
{
	if (resultCode != 0)
	{
		envir() << "Failed to PAUSE: " << resultString << "\n";
		m_callback->onError(m_connection, resultString);
	}
	else
	{
		if (fVerbosityLevel > 1)
		{
			envir() << "PAUSE OK" << "\n";
		}

		// no data will arrive while paused, do not let the data arrival timeout fire,
		// it is scheduled again by the next PLAY (resume, seek or scale change)
		envir().taskScheduler().unscheduleDelayedTask(m_DataArrivalTimeoutTask);
	}
	delete[] resultString;
}

void RTSPConnection::RTSPClientConnection::continueAfterTEARDOWN(int resultCode, char* resultString)
{
	if (resultCode != 0)
	{
		envir() << "Failed to TEARDOWN: " << resultString << "\n";
		m_callback->onError(m_connection, resultString);
	}
	delete[] resultString;
}


void RTSPConnection::RTSPClientConnection::TaskConnectionTimeout()
{
	m_callback->onConnectionTimeout(m_connection);
}
	
void RTSPConnection::RTSPClientConnection::TaskKeepAlive() {
	envir() << "RTSP keep alive\n";	
    this->sendGetParameterCommand(*m_session, NULL, NULL);
    m_KeepAliveTask = envir().taskScheduler().scheduleDelayedTask(m_timeout * 500000, TaskKeepAlive, this);
}

void RTSPConnection::RTSPClientConnection::TaskDataArrivalTimeout()
{
	unsigned int newTotNumPacketsReceived = 0;

	MediaSubsessionIterator iter(*m_session);
	MediaSubsession* subsession;
	while ((subsession = iter.next()) != NULL) 
	{
		RTPSource* src = subsession->rtpSource();
		if (src != NULL) 
		{
			newTotNumPacketsReceived += src->receptionStatsDB().totNumPacketsReceived();
		}
	}
	
	if (newTotNumPacketsReceived == m_nbPacket) 
	{
		m_callback->onDataTimeout(m_connection);
	} 
	else 
	{
		m_nbPacket = newTotNumPacketsReceived;
		m_DataArrivalTimeoutTask = envir().taskScheduler().scheduleDelayedTask(m_timeout*1000000, TaskDataArrivalTimeout, this);
	}	
}
