/*****************************************************************************
 *
 * Copyright (C) 2008-2011 Homer-conferencing project
 *
 * This software is free software.
 * Your are allowed to redistribute it and/or modify it under the terms of
 * the GNU General Public License version 2 as published by the Free Software
 * Foundation.
 *
 * This source is published in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License version 2 for more details.
 *
 * You should have received a copy of the GNU General Public License version 2
 * along with this program. Otherwise, you can write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02111, USA.
 * Alternatively, you find an online version of the license text under
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 *****************************************************************************/

/*
 * Name:    SIP.h
 * Purpose: session initiation protocol
 * Author:  Thomas Volkert
 * Since:   2009-04-14
 * Version: $Id$
 */

#ifndef _CONFERENCE_SIP_
#define _CONFERENCE_SIP_

#include <HBThread.h>
#include <MeetingEvents.h>
#include <SIP_stun.h>
#include <PIDF.h>
#include <Header_SofiaSip.h>

#include <string>

using namespace Homer::Base;

namespace Homer { namespace Conference {

///////////////////////////////////////////////////////////////////////////////

// de/activate STUN
#define SIP_SUPORTING_STUN

// de/activate adaption of source address in case of NAT
//#define SIP_NAT_SOURCE_ADDRESS_ADAPTION

// de/activate strict assertions
//#define SIP_ASSERTS

#define USER_AGENT_SIGNATURE                            "homer-conferencing.com"
#define CALL_REQUEST_RETRIES                            1
#define MESSAGE_REQUEST_RETRIES                         1
#define OPTIONS_REQUEST_RETRIES                         0

enum AvailabilityState{
	AVAILABILITY_STATE_NO = 0,
	AVAILABILITY_STATE_YES = 1,
	AVAILABILITY_STATE_YES_AUTO = 2
};

struct SipContext
{
  su_home_t             Home;           /* memory home */
  su_root_t             *Root;          /* root object */
  nua_t                 *Nua;           /* NUA stack object */
};

///////////////////////////////////////////////////////////////////////////////

class SIP:
    public SIP_stun, public PIDF, public Thread
{
public:
    SIP();

    virtual ~SIP();

    /* presence management */
    void setAvailabilityState(enum AvailabilityState pState, std::string pStateText = "");
    void setAvailabilityState(std::string pState);
    int getAvailabilityState();
    std::string getAvailabilityStateStr();
    /* server based user registration */
    bool RegisterAtServer();
    bool RegisterAtServer(std::string pUsername, std::string pPassword, std::string pServer = "sip2sip.info");
    void UnregisterAtServer();
    bool getServerRegistrationState();
    std::string GetServerSoftwareId();
    /* NAT detection */
    virtual void SetStunServer(std::string pServer);
    /* general */
    static std::string SipCreateId(std::string pUser, std::string pHost, std::string pPort = "");

private:
    /* main SIP event loop handling */
    virtual void* Run(void*);

    /* init. general application events for SIP requests/responses */
    void InitGeneralEvent_FromSipReceivedRequestEvent(const sip_to_t *pRemote, const sip_to_t *pLocal, nua_handle_t *pNuaHandle, sip_t const *pSip, GeneralEvent *pEvent, std::string pEventName, std::string &pSourceIp, unsigned int pSourcePort);
    std::string InitGeneralEvent_FromSipReceivedResponseEvent(const sip_to_t *pRemote, const sip_to_t *pLocal, nua_handle_t *pNuaHandle, sip_t const *pSip, GeneralEvent *pEvent, std::string pEventName, std::string &pSourceIp, unsigned int pSourcePort);

protected:
    void Init(int pStartPort = 5060, int pStunPort = 5070);
    void DeInit();

    void StopSipMainLoop();

    static void SipCallBack(nua_event_t pEvent, int pStatus, char const *pPhrase, nua_t *pNua, nua_magic_t *pMagic, nua_handle_t *pNuaHandle, nua_hmagic_t *pHMagic, sip_t const *pSip, tagi_t pTags[]);

    void PrintSipHeaderInfo(const sip_to_t *pRemote, const sip_to_t *pLocal, sip_t const *pSip);
    void printFromToSendingSipEvent(nua_handle_t *pNuaHandle, GeneralEvent *pEvent, std::string pEventName);
    void initParticipantTriplet(const sip_to_t *pRemote, sip_t const *pSip, std::string &pSourceIp, unsigned int pSourcePort, std::string &pUser, std::string &pHost, std::string &pPort);

    void SipReceivedError(const sip_to_t *pSipRemote, const sip_to_t *pSipLocal, nua_handle_t *pNuaHandle, int pStatus, const char* pPhrase, sip_t const *pSip, std::string pSourceIp, unsigned int pSourcePort);

    void SipReceivedMessage(const sip_to_t *pSipRemote, const sip_to_t *pSipLocal, nua_handle_t *pNuaHandle, sip_t const *pSip, std::string pSourceIp, unsigned int pSourcePort);
    void SipReceivedMessageResponse(const sip_to_t *pSipRemote, const sip_to_t *pSipLocal, nua_handle_t *pNuaHandle, int pStatus, sip_t const *pSip, tagi_t pTags[], std::string pSourceIp, unsigned int pSourcePort);
        /* helpers for "message response */
        void SipReceivedMessageAccept(const sip_to_t *pSipRemote, const sip_to_t *pSipLocal, nua_handle_t *pNuaHandle, sip_t const *pSip, std::string pSourceIp, unsigned int pSourcePort);
        void SipReceivedMessageAcceptDelayed(const sip_to_t *pSipRemote, const sip_to_t *pSipLocal, nua_handle_t *pNuaHandle, sip_t const *pSip, std::string pSourceIp, unsigned int pSourcePort);
        void SipReceivedMessageUnavailable(const sip_to_t *pSipRemote, const sip_to_t *pSipLocal, nua_handle_t *pNuaHandle, sip_t const *pSip, std::string pSourceIp, unsigned int pSourcePort);
        /* */
    void SipReceivedCall(const sip_to_t *pSipRemote, const sip_to_t *pSipLocal, nua_handle_t *pNuaHandle, sip_t const *pSip, tagi_t pTags[], std::string pSourceIp, unsigned int pSourcePort);
    void SipReceivedCallResponse(const sip_to_t *pSipRemote, const sip_to_t *pSipLocal, nua_handle_t *pNuaHandle, int pStatus, const char* pPhrase, sip_t const *pSip, tagi_t pTags[], std::string pSourceIp, unsigned int pSourcePort);
        /* helpers for "call response */
        void SipReceivedCallRinging(const sip_to_t *pSipRemote, const sip_to_t *pSipLocal, nua_handle_t *pNuaHandle, sip_t const *pSip, std::string pSourceIp, unsigned int pSourcePort);
        void SipReceivedCallAccept(const sip_to_t *pSipRemote, const sip_to_t *pSipLocal, nua_handle_t *pNuaHandle, sip_t const *pSip, std::string pSourceIp, unsigned int pSourcePort);
        void SipReceivedCallUnavailable(const sip_to_t *pSipRemote, const sip_to_t *pSipLocal, nua_handle_t *pNuaHandle, sip_t const *pSip, std::string pSourceIp, unsigned int pSourcePort);
        void SipReceivedCallDeny(const sip_to_t *pSipRemote, const sip_to_t *pSipLocal, nua_handle_t *pNuaHandle, sip_t const *pSip, std::string pSourceIp, unsigned int pSourcePort);
        void SipReceivedCallDenyNat(const sip_to_t *pSipRemote, const sip_to_t *pSipLocal, nua_handle_t *pNuaHandle, sip_t const *pSip, std::string pSourceIp, unsigned int pSourcePort, std::string pOwnNatIp, unsigned int pOwnNatPort);
        /* */
    void SipReceivedCallCancel(const sip_to_t *pSipRemote, const sip_to_t *pSipLocal, nua_handle_t *pNuaHandle, sip_t const *pSip, std::string pSourceIp, unsigned int pSourcePort);
    void SipReceivedCallHangup(const sip_to_t *pSipRemote, const sip_to_t *pSipLocal, nua_handle_t *pNuaHandle, sip_t const *pSip, std::string pSourceIp, unsigned int pSourcePort);
    void SipReceivedCallHangupResponse(const sip_to_t *pSipRemote, const sip_to_t *pSipLocal, nua_handle_t *pNuaHandle, int pStatus, sip_t const *pSip, std::string pSourceIp, unsigned int pSourcePort);
    void SipReceivedCallTermination(const sip_to_t *pSipRemote, const sip_to_t *pSipLocal, nua_handle_t *pNuaHandle, sip_t const *pSip, std::string pSourceIp, unsigned int pSourcePort);
    void SipReceivedCallStateChange(const sip_to_t *pSipRemote, const sip_to_t *pSipLocal, nua_handle_t *pNuaHandle, sip_t const *pSip, tagi_t pTags[], std::string pSourceIp, unsigned int pSourcePort);
    void SipReceivedOptionsResponse(const sip_to_t *pSipRemote, const sip_to_t *pSipLocal, nua_handle_t *pNuaHandle, int pStatus, sip_t const *pSip, std::string pSourceIp, unsigned int pSourcePort);
        /* helpers for "options response */
        void SipReceivedOptionsResponseAccept(const sip_to_t *pSipRemote, const sip_to_t *pSipLocal, nua_handle_t *pNuaHandle, sip_t const *pSip, std::string pSourceIp, unsigned int pSourcePort);
        void SipReceivedOptionsResponseUnavailable(const sip_to_t *pSipRemote, const sip_to_t *pSipLocal, nua_handle_t *pNuaHandle, sip_t const *pSip, std::string pSourceIp, unsigned int pSourcePort);
        /* */
    void SipReceivedShutdownResponse(const sip_to_t *pSipRemote, const sip_to_t *pSipLocal, nua_handle_t *pNuaHandle, int pStatus, sip_t const *pSip, std::string pSourceIp, unsigned int pSourcePort);
    void SipReceivedRegisterResponse(const sip_to_t *pSipRemote, const sip_to_t *pSipLocal, nua_handle_t *pNuaHandle, int pStatus, const char* pPhrase, sip_t const *pSip, string pSourceIp, unsigned int pSourcePort);
    void SipReceivedPublishResponse(const sip_to_t *pSipRemote, const sip_to_t *pSipLocal, nua_handle_t *pNuaHandle, int pStatus, const char* pPhrase, sip_t const *pSip, string pSourceIp, unsigned int pSourcePort);
    void SipReceivedAuthenticationResponse(const sip_to_t *pSipRemote, const sip_to_t *pSipLocal, nua_handle_t *pNuaHandle, int pStatus, std::string pSourceIp, unsigned int pSourcePort);
    void SipSendMessage(MessageEvent *pMEvent);
    void SipSendCall(CallEvent *pCEvent);
    void SipSendCallRinging(CallRingingEvent *pCREvent);
    void SipSendCallCancel(CallCancelEvent *pCCEvent);
    void SipSendCallAccept(CallAcceptEvent *pCAEvent);
    void SipSendCallDeny(CallDenyEvent *pCDEvent);
    void SipSendCallHangUp(CallHangUpEvent *pCHUEvent);
    void SipSendOptionsRequest(OptionsEvent *pOEvent);
    void SipProcessOutgoingEvents();

    /* sip registrar support */
    bool SipLoginAtServer();
    void SipLogoutAtServer();

    EventManager        OutgoingEvents; // from users point of view
    enum AvailabilityState mAvailabilityState;
    SipContext          mSipContext;
    std::string         mSipHostAdr;
    nua_handle_t        *mSipRegisterHandle, *mSipPublishHandle;
    std::string         mSipRegisterServer;
    std::string         mSipRegisterServerSoftwareId;
    std::string         mSipRegisterUsername;
    std::string         mSipRegisterPassword;
    sip_payload_t       *mPresenceDesription;
    int                 mSipHostPort;
    bool                mSipListenerNeeded;
    bool                mSipStackOnline;
    int                 mSipRegisteredAtServer; //tri-state: 0 = unregistered, 1 = registered, -1 = registration failed
    bool                mSipPresencePublished; //tri-state: 0 = unpublished, 1 = published, -1 = publication failed
};

///////////////////////////////////////////////////////////////////////////////

}}

#endif
