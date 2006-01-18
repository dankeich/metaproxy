/* $Id: filter_auth_simple.cpp,v 1.15 2006-01-18 14:38:48 mike Exp $
   Copyright (c) 2005, Index Data.

%LICENSE%
 */

#include "config.hpp"

#include "filter.hpp"
#include "package.hpp"

#include <boost/thread/mutex.hpp>
#include <boost/algorithm/string.hpp>

#include "util.hpp"
#include "filter_auth_simple.hpp"

#include <yaz/zgdu.h>
#include <yaz/diagbib1.h>
#include <stdio.h>
#include <errno.h>

namespace yf = yp2::filter;

namespace yp2 {
    namespace filter {
        class AuthSimple::Rep {
            friend class AuthSimple;
            struct PasswordAndDBs {
                std::string password;
                std::list<std::string> dbs;
                PasswordAndDBs() {};
                PasswordAndDBs(std::string pw) : password(pw) {};
                void addDB(std::string db) { dbs.push_back(db); }
            };
            boost::mutex mutex;
            bool got_userRegister, got_targetRegister;
            std::map<std::string, PasswordAndDBs> userRegister;
            std::map<std::string, std::list<std::string> > targetsByUser;
            std::map<yp2::Session, std::string> userBySession;
            bool discardUnauthorisedDBs;
            Rep() { got_userRegister = false;
                    got_targetRegister = false;
                    discardUnauthorisedDBs = false; }
        };
    }
}

yf::AuthSimple::AuthSimple() : m_p(new Rep)
{
    // nothing to do
}

yf::AuthSimple::~AuthSimple()
{  // must have a destructor because of boost::scoped_ptr
}


static void die(std::string s) { throw yp2::filter::FilterException(s); }


// Read XML config.. Put config info in m_p.
void yp2::filter::AuthSimple::configure(const xmlNode * ptr)
{
    std::string userRegisterName;
    std::string targetRegisterName;

    for (ptr = ptr->children; ptr != 0; ptr = ptr->next) {
        if (ptr->type != XML_ELEMENT_NODE)
            continue;
        if (!strcmp((const char *) ptr->name, "userRegister")) {
            userRegisterName = yp2::xml::get_text(ptr);
            m_p->got_userRegister = true;
        } else if (!strcmp((const char *) ptr->name, "targetRegister")) {
            targetRegisterName = yp2::xml::get_text(ptr);
            m_p->got_targetRegister = true;
        } else if (!strcmp((const char *) ptr->name,
                           "discardUnauthorisedDBs")) {
            m_p->discardUnauthorisedDBs = true;
        } else {
            die("Bad element in auth_simple: <"
                + std::string((const char *) ptr->name) + ">");
        }
    }

    if (!m_p->got_userRegister && !m_p->got_targetRegister)
        die("auth_simple: no user-register or target-register "
            "filename specified");

    if (m_p->got_userRegister)
        config_userRegister(userRegisterName);
    if (m_p->got_targetRegister)
        config_targetRegister(targetRegisterName);
}


void yp2::filter::AuthSimple::config_userRegister(std::string filename)
{
    FILE *fp = fopen(filename.c_str(), "r");
    if (fp == 0)
        die("can't open auth_simple user-register '" + filename + "': " +
            strerror(errno));

    char buf[1000];
    while (fgets(buf, sizeof buf, fp)) {
        if (*buf == '\n' || *buf == '#')
            continue;
        buf[strlen(buf)-1] = 0;
        char *passwdp = strchr(buf, ':');
        if (passwdp == 0)
            die("auth_simple user-register '" + filename + "': " +
                "no password on line: '" + buf + "'");
        *passwdp++ = 0;
        char *databasesp = strchr(passwdp, ':');
        if (databasesp == 0)
            die("auth_simple user-register '" + filename + "': " +
                "no databases on line: '" + buf + ":" + passwdp + "'");
        *databasesp++ = 0;
        yf::AuthSimple::Rep::PasswordAndDBs tmp(passwdp);
        boost::split(tmp.dbs, databasesp, boost::is_any_of(","));
        m_p->userRegister[buf] = tmp;

        if (0) {                // debugging
            printf("Added user '%s' -> password '%s'\n", buf, passwdp);
            std::list<std::string>::const_iterator i;
            for (i = tmp.dbs.begin(); i != tmp.dbs.end(); i++) {
                printf("db '%s'\n", (*i).c_str());
            }
        }
    }
}


// I feel a little bad about the duplication of code between this and
// config_userRegister().  But not bad enough to refactor.
//
void yp2::filter::AuthSimple::config_targetRegister(std::string filename)
{
    FILE *fp = fopen(filename.c_str(), "r");
    if (fp == 0)
        die("can't open auth_simple target-register '" + filename + "': " +
            strerror(errno));

    char buf[1000];
    while (fgets(buf, sizeof buf, fp)) {
        if (*buf == '\n' || *buf == '#')
            continue;
        buf[strlen(buf)-1] = 0;
        char *targetsp = strchr(buf, ':');
        if (targetsp == 0)
            die("auth_simple target-register '" + filename + "': " +
                "no targets on line: '" + buf + "'");
        *targetsp++ = 0;
        std::list<std::string> tmp;
        boost::split(tmp, targetsp, boost::is_any_of(","));
        m_p->targetsByUser[buf] = tmp;

        if (0) {                // debugging
            printf("Added user '%s' with targets:\n", buf);
            std::list<std::string>::const_iterator i;
            for (i = tmp.begin(); i != tmp.end(); i++) {
                printf("\t%s\n", (*i).c_str());
            }
        }
    }
}


void yf::AuthSimple::process(yp2::Package &package) const
{
    Z_GDU *gdu = package.request().get();

    if (!gdu || gdu->which != Z_GDU_Z3950) {
        // Pass on the package -- This means that authentication is
        // waived, which may not be the correct thing for non-Z APDUs
        // as it means that SRW sessions don't demand authentication
        return package.move();
    }

    if (m_p->got_userRegister) {
        switch (gdu->u.z3950->which) {
        case Z_APDU_initRequest: return process_init(package);
        case Z_APDU_searchRequest: return process_search(package);
        case Z_APDU_scanRequest: return process_scan(package);
            // In theory, we should check database authorisation for
            // extended services, too (A) the proxy currently does not
            // implement XS and turns off its negotiation bit; (B) it
            // would be insanely complex to do as the top-level XS request
            // structure does not carry a database name, but it is buried
            // down in some of the possible EXTERNALs used as
            // taskSpecificParameters; and (C) since many extended
            // services modify the database, we'd need to more exotic
            // authorisation database than we want to support.
        default: break;
        }
    }

    if (m_p->got_targetRegister && gdu->u.z3950->which == Z_APDU_initRequest)
        return check_targets(package);

    // Operations other than those listed above do not require authorisation
    return package.move();
}


static void reject_init(yp2::Package &package, int err, const char *addinfo);


void yf::AuthSimple::process_init(yp2::Package &package) const
{
    Z_IdAuthentication *auth =
        package.request().get()->u.z3950->u.initRequest->idAuthentication;
        // This is just plain perverted.

    if (!auth)
        return reject_init(package, 0, "no credentials supplied");
    if (auth->which != Z_IdAuthentication_idPass)
        return reject_init(package, 0,
                           "only idPass authentication is supported");
    Z_IdPass *idPass = auth->u.idPass;

    if (m_p->userRegister.count(idPass->userId)) {
        // groupId is ignored, in accordance with ancient tradition.
        yf::AuthSimple::Rep::PasswordAndDBs pdbs =
            m_p->userRegister[idPass->userId];
        if (pdbs.password == idPass->password) {
            // Success!  Remember who the user is for future reference
            {
                boost::mutex::scoped_lock lock(m_p->mutex);
                m_p->userBySession[package.session()] = idPass->userId;
            }
            return package.move();
        }
    }

    return reject_init(package, 0, "username/password combination rejected");
}


// I find it unutterable disappointing that I have to provide this
static bool contains(std::list<std::string> list, std::string thing) {
    std::list<std::string>::const_iterator i;
    for (i = list.begin(); i != list.end(); i++)
        if (*i == thing)
            return true;

    return false;
}


void yf::AuthSimple::process_search(yp2::Package &package) const
{
    Z_SearchRequest *req =
        package.request().get()->u.z3950->u.searchRequest;

    if (m_p->userBySession.count(package.session()) == 0) {
        // It's a non-authenticated session, so just accept the operation
        return package.move();
    }

    std::string user = m_p->userBySession[package.session()];
    yf::AuthSimple::Rep::PasswordAndDBs pdb = m_p->userRegister[user];
    for (int i = 0; i < req->num_databaseNames; i++) {
        if (!contains(pdb.dbs, req->databaseNames[i]) &&
            !contains(pdb.dbs, "*")) {
            // Make an Search rejection APDU
            yp2::odr odr;
            Z_APDU *apdu = odr.create_searchResponse(
                package.request().get()->u.z3950, 
                YAZ_BIB1_ACCESS_TO_SPECIFIED_DATABASE_DENIED,
                req->databaseNames[i]);
            package.response() = apdu;
            package.session().close();
            return;
        }
    }

    // All the requested databases are acceptable
    return package.move();
}


void yf::AuthSimple::process_scan(yp2::Package &package) const
{
    Z_ScanRequest *req =
        package.request().get()->u.z3950->u.scanRequest;

    if (m_p->userBySession.count(package.session()) == 0) {
        // It's a non-authenticated session, so just accept the operation
        return package.move();
    }

    std::string user = m_p->userBySession[package.session()];
    yf::AuthSimple::Rep::PasswordAndDBs pdb = m_p->userRegister[user];
    for (int i = 0; i < req->num_databaseNames; i++) {
        if (!contains(pdb.dbs, req->databaseNames[i]) &&
            !contains(pdb.dbs, "*")) {
            // Make an Scan rejection APDU
            yp2::odr odr;
            Z_APDU *apdu = odr.create_scanResponse(
                package.request().get()->u.z3950, 
                YAZ_BIB1_ACCESS_TO_SPECIFIED_DATABASE_DENIED,
                req->databaseNames[i]);
            package.response() = apdu;
            package.session().close();
            return;
        }
    }

    // All the requested databases are acceptable
    return package.move();
}


static void reject_init(yp2::Package &package, int err, const char *addinfo) { 
    if (err == 0)
        err = YAZ_BIB1_INIT_AC_AUTHENTICATION_SYSTEM_ERROR;
    // Make an Init rejection APDU
    Z_GDU *gdu = package.request().get();
    yp2::odr odr;
    Z_APDU *apdu = odr.create_initResponse(gdu->u.z3950, err, addinfo);
    apdu->u.initResponse->implementationName = "YP2/YAZ";
    *apdu->u.initResponse->result = 0; // reject
    package.response() = apdu;
    package.session().close();
}


void yf::AuthSimple::check_targets(yp2::Package & package) const
{
    Z_InitRequest *initReq = package.request().get()->u.z3950->u.initRequest;

    Z_IdAuthentication *auth = initReq->idAuthentication;
    // We only get into this method if we are dealing with a session
    // that has been authenticated using idPass authentication.  So we
    // know what kind of information is in the Init Request, and we
    // can trust the username without needing to re-authenticate.
    assert(auth->which == Z_IdAuthentication_idPass);
    std::string user = auth->u.idPass->userId;
    std::list<std::string> authorisedTargets = m_p->targetsByUser[user];

    std::list<std::string> targets;
    Z_OtherInformation *otherInfo = initReq->otherInfo;
    yp2::util::get_vhost_otherinfo(&otherInfo, 0, targets);

    // Check each of the targets specified in the otherInfo package
    std::list<std::string>::iterator i;

    printf("pre: got %d targets\n", targets.size());
    i = targets.begin();
    while (i != targets.end()) {
        printf("pre: considering target '%s'\n", (*i).c_str());
        i++;
    }

    i = targets.begin();
    while (i != targets.end()) {
        printf("checking target '%s'\n", (*i).c_str());
        if (contains(authorisedTargets, *i) ||
            contains(authorisedTargets, "*")) {
            printf("target '%s' is ok\n", (*i).c_str());
            i++;
        } else {
            printf("target '%s' sucks\n", (*i).c_str());
            if (!m_p->discardUnauthorisedDBs)
                return reject_init(package,
                    YAZ_BIB1_ACCESS_TO_SPECIFIED_DATABASE_DENIED, i->c_str());
            i = targets.erase(i);
        }
    }

    printf("post: got %d targets\n", targets.size());
    i = targets.begin();
    while (i != targets.end()) {
        printf("post: considering target '%s'\n", (*i).c_str());
        i++;
    }

    if (targets.size() == 0)
        return reject_init(package,
                           YAZ_BIB1_ACCESS_TO_SPECIFIED_DATABASE_DENIED,
                           "all databases");

    // ### This is a no-op if the list has not changed
    yp2::odr odr;
    yp2::util::set_vhost_otherinfo(&otherInfo, odr, targets);
    package.move();
}


static yp2::filter::Base* filter_creator()
{
    return new yp2::filter::AuthSimple;
}

extern "C" {
    struct yp2_filter_struct yp2_filter_auth_simple = {
        0,
        "auth_simple",
        filter_creator
    };
}


/*
 * Local variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * c-file-style: "stroustrup"
 * End:
 * vim: shiftwidth=4 tabstop=8 expandtab
 */
