/* $Id: thread_pool_observer.hpp,v 1.10 2006-06-10 14:29:13 adam Exp $
   Copyright (c) 2005-2006, Index Data.

   See the LICENSE file for details
 */

#ifndef YP2_THREAD_POOL_OBSERVER_HPP
#define YP2_THREAD_POOL_OBSERVER_HPP

#include <boost/scoped_ptr.hpp>

#include <yazpp/socket-observer.h>
#include <yaz/yconfig.h>

namespace metaproxy_1 {
    class IThreadPoolMsg {
    public:
        virtual IThreadPoolMsg *handle() = 0;
        virtual void result() = 0;
        virtual ~IThreadPoolMsg();
    };

    class ThreadPoolSocketObserver : public yazpp_1::ISocketObserver {
        class Rep;
        class Worker;
    public:
        ThreadPoolSocketObserver(yazpp_1::ISocketObservable *obs,
                                 int no_threads);
        virtual ~ThreadPoolSocketObserver();
        void put(IThreadPoolMsg *m);
        IThreadPoolMsg *get();
        void run(void *p);
    private:
        void socketNotify(int event);
        boost::scoped_ptr<Rep> m_p;

    };
}
#endif
/*
 * Local variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * c-file-style: "stroustrup"
 * End:
 * vim: shiftwidth=4 tabstop=8 expandtab
 */

