
#ifndef FILTER_FRONTEND_NET_HPP
#define FILTER_FRONEND_NET_HPP

#include <stdexcept>

#include "filter.hpp"

namespace yp2 {
    class FilterFrontendNet : public yp2::Filter {
    public:
	FilterFrontendNet::FilterFrontendNet();
	void process(yp2::Package & package) const;
    private:
        int m_no_threads;
        std::string m_listen_address;
        int m_listen_duration;
    public:
        /// set function - left val in assignment
        std::string & listen_address();
        int &listen_duration();
    };
}


#endif
/*
 * Local variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim: shiftwidth=4 tabstop=8 expandtab
 */
