/* ---------------------------------------------------------------------------
** This software is in the public domain, furnished "as is", without technical
** support, and with no warranty, express or implied, as to its usefulness for
** any purpose.
**
** rtspconnectionclient.h
** 
** Interface to an RTSP client connection
** 
** -------------------------------------------------------------------------*/

#pragma once

#include "environment.h"
#include "SessionSink.h"
#include "liveMedia.hh"
#include <atomic>
#include <string>
#include <map>
#include <vector>

#define RTSP_CALLBACK(uri, resultCode, resultString) \
static void continueAfter ## uri(RTSPClient* rtspClient, int resultCode, char* resultString) { static_cast<RTSPConnection::RTSPClientConnection*>(rtspClient)->continueAfter ## uri(resultCode, resultString); } \
void continueAfter ## uri (int resultCode, char* resultString); \
/**/

#define TASK_CALLBACK(class,task) \
TaskToken m_ ## task ## Task; \
static void Task ## task(void* rtspClient) { static_cast<class*>(rtspClient)->Task ## task(); } \
void Task ## task (); \
/**/


#if LIVEMEDIA_LIBRARY_VERSION_INT > 1371168000 
	#define RTSPClientConstrutor(env, url, verbosity, appname, httpTunnelPort) RTSPClient(env, url, verbosity, appname, httpTunnelPort ,-1)
#else					
	#define RTSPClientConstrutor(env, url, verbosity, appname, httpTunnelPort) RTSPClient(env, url, verbosity, appname, httpTunnelPort)
#endif

/* ---------------------------------------------------------------------------
**  RTSP client connection interface
** -------------------------------------------------------------------------*/
class RTSPConnection 
{
	public:
		enum {
			RTPUDPUNICAST,
			RTPUDPMULTICAST,
			RTPOVERTCP,
			RTPOVERHTTP
		};
	
		static int decodeTimeoutOption(const std::map<std::string,std::string> & opts) {
			int timeout = 10;
			auto it = opts.find("timeout");
			if (it != opts.end())
			{
				timeout = std::stoi(it->second);
			}
			return timeout;
		}

		static int decodeRTPTransport(const std::map<std::string,std::string> & opts)
		{
			int rtptransport = RTSPConnection::RTPUDPUNICAST;
			auto it = opts.find("rtptransport");
			if (it != opts.end())
			{
				const std::string& rtpTransportString = it->second;
				if (rtpTransportString == "tcp") {
					rtptransport = RTSPConnection::RTPOVERTCP;
				} else if (rtpTransportString == "http") {
					rtptransport = RTSPConnection::RTPOVERHTTP;
				} else if (rtpTransportString == "multicast") {
					rtptransport = RTSPConnection::RTPUDPMULTICAST;
				}
			}
			return rtptransport;
		}

		/* ---------------------------------------------------------------------------
		**  RTSP client callback interface
		** -------------------------------------------------------------------------*/
		class Callback : public SessionCallback
		{
			public:
				virtual void    onError(RTSPConnection&, const char*)  {}
				virtual void    onConnectionTimeout(RTSPConnection&)   {}
				virtual void    onDataTimeout(RTSPConnection&)         {}
				virtual void    onEndOfFile(RTSPConnection&)           {}
				virtual void    onPlayUpdate(RTSPConnection&)          {}
		};
	
		/* ---------------------------------------------------------------------------
		**  RTSP client 
		** -------------------------------------------------------------------------*/
		class RTSPClientConnection : public RTSPClient
		{
			public:
				RTSPClientConnection(RTSPConnection& connection, Environment& env, Callback* callback, const char* rtspURL, int timeout, int rtptransport, int verbosityLevel = 0);
				virtual ~RTSPClientConnection();

				void setStartTime(const std::string& clockStartTime, double nptStartTime = -1.0);
				void setScale(float scale);

				bool isClockMode() const { return m_clockMode; }
				float getScale() const { return m_scale; };
				double getNormalPlayTime(struct timeval const& presentationTime);
				// duration of the play range in seconds from the SDP/PLAY answer,
				// 0 when unknown (clock mode: compute it from the clock times of the url)
				double getPlayDuration() const;
				// playback speeds supported by the server, from the "Scales=" list of the
				// "Media-Properties:" answer (empty when the server does not answer it)
				std::vector<float> getSupportedScales() const;

				void sendPlay();
				void sendPause();

			protected:
				void sendNextCommand();
				void setNptstartTime();

				RTSP_CALLBACK(OPTIONS, resultCode, resultString);
				RTSP_CALLBACK(DESCRIBE, resultCode, resultString);
				RTSP_CALLBACK(SETUP, resultCode, resultString);
				RTSP_CALLBACK(PLAY, resultCode, resultString);
				RTSP_CALLBACK(PAUSE, resultCode, resultString);
				RTSP_CALLBACK(TEARDOWN, resultCode, resultString);
			
				TASK_CALLBACK(RTSPConnection::RTSPClientConnection,ConnectionTimeout);
				TASK_CALLBACK(RTSPConnection::RTSPClientConnection,DataArrivalTimeout);
				TASK_CALLBACK(RTSPConnection::RTSPClientConnection,KeepAlive);
				
			protected:
				RTSPConnection&          m_connection;
				int                      m_timeout;
				int                      m_rtptransport;
				MediaSession*      		 m_session;
				MediaSubsession*         m_subSession;
				MediaSubsession*         m_nptSubsession;           ///Subsession computing the normal play time (the NPT is identical for all the tracks)
				MediaSubsessionIterator* m_subSessionIter;
				Callback*                m_callback; 	
				unsigned int             m_nbPacket;
				double					 m_nptStartTime;			///Next Play Time start time (in seconds) to use in the next PLAY command
				std::string				 m_clockStartTime;			///Next Play Time start time (in clock time) to use in the next PLAY command
				std::string				 m_clockEndTime;			///Next Play Time end time (in clock time) to use in the next PLAY command
				float                    m_scale;					///Current playback speed
				bool                     m_getparameterSupported;
				bool					 m_clockMode;				///True if the playback is in clock mode (range start time is a clock time)
		};
		
	public:
		RTSPConnection(Environment& env, Callback* callback, const char* rtspURL, const std::map<std::string,std::string> & opts = std::map<std::string,std::string>(), int verbosityLevel = 1);
		virtual ~RTSPConnection();

		void        start(unsigned int delay = 0);
		void		stop();
		std::string getUrl() const          { return m_url; }
		int         getRtpTransport() const { return m_rtptransport; }
		const RTSPClientConnection* getClient() const { return m_rtspClient; }

		// change the playback speed on a running connection (RTSP "Scale" header)
		void        scale(float scale);
		// seek to a position in seconds from the beginning (RTSP "Range" header)
		void        seek(double nptStartTime);
		void        seek(const std::string& clockStartTime);
		// pause the stream (RTSP "PAUSE" command)
		void        pause();
		// resume a paused stream (RTSP "PLAY" command without "Range" header)
		void        resume();
		// normal play time of a frame (seconds from the start of the presentation),
		// negative when the position cannot be computed yet ("RTP-Info:" missing or
		// presentation time not synchronized using RTCP)
		double      getNormalPlayTime(const char* id, struct timeval presentationTime);
		// playback speeds supported by the server (see RTSPClientConnection)
		std::vector<float> getSupportedScales() const;

	protected:
		TASK_CALLBACK(RTSPConnection,startCallback);
		TASK_CALLBACK(RTSPConnection,stopCallback);
		TASK_CALLBACK(RTSPConnection,Play);
		TASK_CALLBACK(RTSPConnection,Pause);

	protected:
		Environment&             m_env;
		Callback*                m_callback;
		std::string              m_url;
		int                      m_timeout;
		int                      m_rtptransport;
		int                      m_verbosity;

		RTSPClientConnection*    m_rtspClient;
};
