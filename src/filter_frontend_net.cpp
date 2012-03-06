/* This file is part of Metaproxy.
   Copyright (C) 2005-2012 Index Data

Metaproxy is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 2, or (at your option) any later
version.

Metaproxy is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include "config.hpp"

#include <sstream>
#include <iomanip>
#include <metaproxy/util.hpp>
#include "pipe.hpp"
#include <metaproxy/filter.hpp>
#include <metaproxy/package.hpp>
#include "thread_pool_observer.hpp"
#include "filter_frontend_net.hpp"
#include <yazpp/z-assoc.h>
#include <yazpp/pdu-assoc.h>
#include <yazpp/socket-manager.h>
#include <yazpp/limit-connect.h>
#include <yaz/timing.h>
#include <yaz/log.h>

#include <iostream>

namespace mp = metaproxy_1;

namespace metaproxy_1 {
    class My_Timer_Thread;
    class ZAssocServer;
    namespace filter {
        class FrontendNet::Port {
            friend class Rep;
            friend class FrontendNet;
            std::string port;
            std::string route;
        };
        class FrontendNet::Rep {
            friend class FrontendNet;
            int m_no_threads;
            std::vector<Port> m_ports;
            int m_listen_duration;
            int m_session_timeout;
            int m_connect_max;
            std::string m_msg_config;
            yazpp_1::SocketManager mySocketManager;
            ZAssocServer **az;
        };
    }
    class My_Timer_Thread : public yazpp_1::ISocketObserver {
    private:
        yazpp_1::ISocketObservable *m_obs;
        Pipe m_pipe;
        bool m_timeout;
    public:
        My_Timer_Thread(yazpp_1::ISocketObservable *obs, int duration);
        void socketNotify(int event);
        bool timeout();
    };
    class ZAssocChild : public yazpp_1::Z_Assoc {
    public:
        ~ZAssocChild();
        ZAssocChild(yazpp_1::IPDU_Observable *the_PDU_Observable,
                    mp::ThreadPoolSocketObserver *m_thread_pool_observer,
                    const mp::Package *package,
                    std::string route,
                    const char *msg_config);
        int m_no_requests;
        std::string m_route;
    private:
        yazpp_1::IPDU_Observer* sessionNotify(
            yazpp_1::IPDU_Observable *the_PDU_Observable,
            int fd);
        void recv_GDU(Z_GDU *apdu, int len);
        
        void failNotify();
        void timeoutNotify();
        void connectNotify();
    private:
        mp::ThreadPoolSocketObserver *m_thread_pool_observer;
        mp::Session m_session;
        mp::Origin m_origin;
        bool m_delete_flag;
        const mp::Package *m_package;
        const char *m_msg_config;
    };
    class ThreadPoolPackage : public mp::IThreadPoolMsg {
    public:
        ThreadPoolPackage(mp::Package *package, mp::ZAssocChild *ses,
            const char *msg_config);
        ~ThreadPoolPackage();
        IThreadPoolMsg *handle();
        void result(const char *t_info);
        
    private:
        yaz_timing_t timer;
        mp::ZAssocChild *m_assoc_child;
        mp::Package *m_package;
        const char *m_msg_config;        
    };
    class ZAssocServer : public yazpp_1::Z_Assoc {
    public:
        ~ZAssocServer();
        ZAssocServer(yazpp_1::IPDU_Observable *PDU_Observable, int timeout,
                     int connect_max, std::string route,
                     const char *msg_config);
        void set_package(const mp::Package *package);
        void set_thread_pool(ThreadPoolSocketObserver *m_thread_pool_observer);
    private:
        yazpp_1::IPDU_Observer* sessionNotify(
            yazpp_1::IPDU_Observable *the_PDU_Observable,
            int fd);
        void recv_GDU(Z_GDU *apdu, int len);
        
        void failNotify();
        void timeoutNotify();
    void connectNotify();
    private:
        mp::ThreadPoolSocketObserver *m_thread_pool_observer;
        const mp::Package *m_package;
        int m_session_timeout;
        int m_connect_max;
        yazpp_1::LimitConnect limit_connect;
        std::string m_route;
        const char *m_msg_config;
    };
}

mp::ThreadPoolPackage::ThreadPoolPackage(mp::Package *package,
                                         mp::ZAssocChild *ses,
                                         const char *msg_config) :
    m_assoc_child(ses), m_package(package), m_msg_config(msg_config)
{
    if (msg_config)
        timer = yaz_timing_create();
    else
        timer = 0;
}

mp::ThreadPoolPackage::~ThreadPoolPackage()
{
    yaz_timing_destroy(&timer); // timer may be NULL
    delete m_package;
}

void mp::ThreadPoolPackage::result(const char *t_info)
{
    m_assoc_child->m_no_requests--;

    yazpp_1::GDU *gdu = &m_package->response();

    if (gdu->get())
    {
	int len;
	m_assoc_child->send_GDU(gdu->get(), &len);
    }
    else if (!m_package->session().is_closed())
    {
        // no response package and yet the session is still open..
        // means that request is unhandled..
        yazpp_1::GDU *gdu_req = &m_package->request();
        Z_GDU *z_gdu = gdu_req->get();
        if (z_gdu && z_gdu->which == Z_GDU_Z3950)
        {
            // For Z39.50, response with a Close and shutdown
            mp::odr odr;
            int len;
            Z_APDU *apdu_response = odr.create_close(
                z_gdu->u.z3950, Z_Close_systemProblem, 
                "unhandled Z39.50 request");

            m_assoc_child->send_Z_PDU(apdu_response, &len);
        }
        else if (z_gdu && z_gdu->which == Z_GDU_HTTP_Request)
        {
            // For HTTP, respond with Server Error
            int len;
            mp::odr odr;
            Z_GDU *zgdu_res 
                = odr.create_HTTP_Response(m_package->session(), 
                                           z_gdu->u.HTTP_Request, 500);
            m_assoc_child->send_GDU(zgdu_res, &len);
        }
        m_package->session().close();
    }

    if (m_assoc_child->m_no_requests == 0 && m_package->session().is_closed())
    {
        m_assoc_child->close();
    }

    if (m_msg_config)
    {
        yaz_timing_stop(timer);
        double duration = yaz_timing_get_real(timer);

        std::ostringstream os;
        os  << m_msg_config << " "
            << *m_package << " "
            << std::fixed << std::setprecision (6) << duration;
        
        yaz_log(YLOG_LOG, "%s %s", os.str().c_str(), t_info);
    }

    delete this;
}

mp::IThreadPoolMsg *mp::ThreadPoolPackage::handle() 
{
    m_package->move(m_assoc_child->m_route);
    return this;
}


mp::ZAssocChild::ZAssocChild(yazpp_1::IPDU_Observable *PDU_Observable,
                             mp::ThreadPoolSocketObserver *my_thread_pool,
                             const mp::Package *package,
                             std::string route,
                             const char *msg_config)
    :  Z_Assoc(PDU_Observable), m_msg_config(msg_config)
{
    m_thread_pool_observer = my_thread_pool;
    m_no_requests = 0;
    m_delete_flag = false;
    m_package = package;
    m_route = route;
    const char *peername = PDU_Observable->getpeername();
    if (!peername)
        peername = "unknown";
    m_origin.set_tcpip_address(std::string(peername), m_session.id());
}


yazpp_1::IPDU_Observer *mp::ZAssocChild::sessionNotify(yazpp_1::IPDU_Observable
						  *the_PDU_Observable, int fd)
{
    return 0;
}

mp::ZAssocChild::~ZAssocChild()
{
}

void mp::ZAssocChild::recv_GDU(Z_GDU *z_pdu, int len)
{
    m_no_requests++;

    mp::Package *p = new mp::Package(m_session, m_origin);

    mp::ThreadPoolPackage *tp = new mp::ThreadPoolPackage(p, this,
                                                          m_msg_config);
    p->copy_route(*m_package);
    p->request() = yazpp_1::GDU(z_pdu);
    m_thread_pool_observer->put(tp);  
}

void mp::ZAssocChild::failNotify()
{
    // TODO: send Package to signal "close"
    if (m_session.is_closed())
    {
        if (m_no_requests == 0)
            delete this;
	return;
    }
    m_no_requests++;

    m_session.close();

    mp::Package *p = new mp::Package(m_session, m_origin);

    mp::ThreadPoolPackage *tp = new mp::ThreadPoolPackage(p, this,
                                                          m_msg_config);
    p->copy_route(*m_package);
    m_thread_pool_observer->put(tp);  
}

void mp::ZAssocChild::timeoutNotify()
{
    failNotify();
}

void mp::ZAssocChild::connectNotify()
{

}

mp::ZAssocServer::ZAssocServer(yazpp_1::IPDU_Observable *PDU_Observable,
                               int timeout, int connect_max,
                               std::string route, const char *msg_config)
    :  Z_Assoc(PDU_Observable), m_session_timeout(timeout),
       m_connect_max(connect_max), m_route(route), m_msg_config(msg_config)
{
    m_package = 0;
}


void mp::ZAssocServer::set_package(const mp::Package *package)
{
    m_package = package;
}

void mp::ZAssocServer::set_thread_pool(ThreadPoolSocketObserver *observer)
{
    m_thread_pool_observer = observer;
}

yazpp_1::IPDU_Observer *mp::ZAssocServer::sessionNotify(yazpp_1::IPDU_Observable
						 *the_PDU_Observable, int fd)
{

    const char *peername = the_PDU_Observable->getpeername();
    if (peername)
    {
        limit_connect.add_connect(peername);
        limit_connect.cleanup(false);
        int con_sz = limit_connect.get_total(peername);
        if (m_connect_max && con_sz > m_connect_max)
            return 0;
    }
    mp::ZAssocChild *my =
	new mp::ZAssocChild(the_PDU_Observable, m_thread_pool_observer,
                            m_package, m_route, m_msg_config);
    my->timeout(m_session_timeout);
    return my;
}

mp::ZAssocServer::~ZAssocServer()
{
}

void mp::ZAssocServer::recv_GDU(Z_GDU *apdu, int len)
{
}

void mp::ZAssocServer::failNotify()
{
}

void mp::ZAssocServer::timeoutNotify()
{
}

void mp::ZAssocServer::connectNotify()
{
}

mp::filter::FrontendNet::FrontendNet() : m_p(new Rep)
{
    m_p->m_no_threads = 5;
    m_p->m_listen_duration = 0;
    m_p->m_session_timeout = 300; // 5 minutes
    m_p->m_connect_max = 0;
    m_p->az = 0;
}

mp::filter::FrontendNet::~FrontendNet()
{
    if (m_p->az)
    {
        size_t i;
        for (i = 0; i<m_p->m_ports.size(); i++)
            delete m_p->az[i];
        delete [] m_p->az;
    }
}

bool mp::My_Timer_Thread::timeout()
{
    return m_timeout;
}

mp::My_Timer_Thread::My_Timer_Thread(yazpp_1::ISocketObservable *obs,
				 int duration) : 
    m_obs(obs), m_pipe(9123), m_timeout(false)
{
    obs->addObserver(m_pipe.read_fd(), this);
    obs->maskObserver(this, yazpp_1::SOCKET_OBSERVE_READ);
    obs->timeoutObserver(this, duration);
}

void mp::My_Timer_Thread::socketNotify(int event)
{
    m_timeout = true;
    m_obs->deleteObserver(this);
}

void mp::filter::FrontendNet::process(Package &package) const
{
    if (m_p->az == 0)
        return;
    size_t i;
    My_Timer_Thread *tt = 0;

    if (m_p->m_listen_duration)
        tt = new My_Timer_Thread(&m_p->mySocketManager,
                                 m_p->m_listen_duration);
    
    ThreadPoolSocketObserver tp(&m_p->mySocketManager, m_p->m_no_threads);

    for (i = 0; i<m_p->m_ports.size(); i++)
    {
	m_p->az[i]->set_package(&package);
	m_p->az[i]->set_thread_pool(&tp);
    }
    while (m_p->mySocketManager.processEvent() > 0)
    {
	if (tt && tt->timeout())
	    break;
    }
    delete tt;
}

void mp::filter::FrontendNet::configure(const xmlNode * ptr, bool test_only,
                                        const char *path)
{
    if (!ptr || !ptr->children)
    {
        throw mp::filter::FilterException("No ports for Frontend");
    }
    std::vector<Port> ports;
    for (ptr = ptr->children; ptr; ptr = ptr->next)
    {
        if (ptr->type != XML_ELEMENT_NODE)
            continue;
        if (!strcmp((const char *) ptr->name, "port"))
        {
            Port port;
            const struct _xmlAttr *attr;
            for (attr = ptr->properties; attr; attr = attr->next)
            {
                if (!strcmp((const char *) attr->name, "route"))
                    port.route = mp::xml::get_text(attr);
            }
            port.port = mp::xml::get_text(ptr);
            ports.push_back(port);
            
        }
        else if (!strcmp((const char *) ptr->name, "threads"))
        {
            std::string threads_str = mp::xml::get_text(ptr);
            int threads = atoi(threads_str.c_str());
            if (threads < 1)
                throw mp::filter::FilterException("Bad value for threads: " 
                                                   + threads_str);
            m_p->m_no_threads = threads;
        }
        else if (!strcmp((const char *) ptr->name, "timeout"))
        {
            std::string timeout_str = mp::xml::get_text(ptr);
            int timeout = atoi(timeout_str.c_str());
            if (timeout < 1)
                throw mp::filter::FilterException("Bad value for timeout: " 
                                                   + timeout_str);
            m_p->m_session_timeout = timeout;
        }
        else if (!strcmp((const char *) ptr->name, "connect-max"))
        {
            m_p->m_connect_max = mp::xml::get_int(ptr, 0);
        }
        else if (!strcmp((const char *) ptr->name, "message"))
        {
            m_p->m_msg_config = mp::xml::get_text(ptr);
        }
        else
        {
            throw mp::filter::FilterException("Bad element " 
                                               + std::string((const char *)
                                                             ptr->name));
        }
    }
    if (test_only)
        return;
    set_ports(ports);
}

void mp::filter::FrontendNet::set_ports(std::vector<std::string> &ports)
{
    std::vector<Port> nports;
    size_t i;

    for (i = 0; i < ports.size(); i++)
    {
        Port nport;

        nport.port = ports[i];

        nports.push_back(nport);
    }
    set_ports(nports);
}


void mp::filter::FrontendNet::set_ports(std::vector<Port> &ports)
{
    m_p->m_ports = ports;
    
    m_p->az = new mp::ZAssocServer *[m_p->m_ports.size()];
    
    // Create mp::ZAssocServer for each port
    size_t i;
    for (i = 0; i<m_p->m_ports.size(); i++)
    {
        // create a PDU assoc object (one per mp::ZAssocServer)
        yazpp_1::PDU_Assoc *as = new yazpp_1::PDU_Assoc(&m_p->mySocketManager);
        
        // create ZAssoc with PDU Assoc
        m_p->az[i] = new mp::ZAssocServer(as, 
                                          m_p->m_session_timeout,
                                          m_p->m_connect_max,
                                          m_p->m_ports[i].route,
                                          m_p->m_msg_config.length() > 0 ?
                                          m_p->m_msg_config.c_str() : 0);
        if (m_p->az[i]->server(m_p->m_ports[i].port.c_str()))
        {
            throw mp::filter::FilterException("Unable to bind to address " 
                                              + std::string(m_p->m_ports[i].port));
        }
    }
}

void mp::filter::FrontendNet::set_listen_duration(int d)
{
    m_p->m_listen_duration = d;
}

static mp::filter::Base* filter_creator()
{
    return new mp::filter::FrontendNet;
}

extern "C" {
    struct metaproxy_1_filter_struct metaproxy_1_filter_frontend_net = {
        0,
        "frontend_net",
        filter_creator
    };
}

/*
 * Local variables:
 * c-basic-offset: 4
 * c-file-style: "Stroustrup"
 * indent-tabs-mode: nil
 * End:
 * vim: shiftwidth=4 tabstop=8 expandtab
 */

