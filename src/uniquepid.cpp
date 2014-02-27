/****************************************************************************
 *   Copyright (C) 2006-2013 by Jason Ansel, Kapil Arya, and Gene Cooperman *
 *   jansel@csail.mit.edu, kapil@ccs.neu.edu, gene@ccs.neu.edu              *
 *                                                                          *
 *   This file is part of the dmtcp/src module of DMTCP (DMTCP:dmtcp/src).  *
 *                                                                          *
 *  DMTCP:dmtcp/src is free software: you can redistribute it and/or        *
 *  modify it under the terms of the GNU Lesser General Public License as   *
 *  published by the Free Software Foundation, either version 3 of the      *
 *  License, or (at your option) any later version.                         *
 *                                                                          *
 *  DMTCP:dmtcp/src is distributed in the hope that it will be useful,      *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 *  GNU Lesser General Public License for more details.                     *
 *                                                                          *
 *  You should have received a copy of the GNU Lesser General Public        *
 *  License along with DMTCP:dmtcp/src.  If not, see                        *
 *  <http://www.gnu.org/licenses/>.                                         *
 ****************************************************************************/

#include <stdlib.h>
#include <iomanip>
#include <pwd.h>
#include "uniquepid.h"
#include "constants.h"
#include "../jalib/jconvert.h"
#include "../jalib/jfilesystem.h"
#include "../jalib/jserialize.h"
#include "syscallwrappers.h"
#include "protectedfds.h"

using namespace dmtcp;

void dmtcp_UniquePid_EventHook(DmtcpEvent_t event, DmtcpEventData_t *data)
{
  switch (event) {
    case DMTCP_EVENT_THREADS_SUSPEND:
      UniquePid::createCkptDir();
      break;

    case DMTCP_EVENT_RESTART:
      dmtcp::UniquePid::restart();
      break;

    default:
      break;
  }
}

static dmtcp::string& _ckptDir()
{
  static dmtcp::string str;
  return str;
}

static dmtcp::string& _uniqueDir()
{
  static dmtcp::string str;
  return str;
}

static dmtcp::string& _ckptFileName()
{
  static dmtcp::string str;
  return str;
}

static dmtcp::string& _ckptFilesSubDir()
{
  static dmtcp::string str;
  return str;
}


inline static long theUniqueHostId()
{
#ifdef USE_GETHOSTID
  return ::gethostid()
#else
  //gethostid() calls socket() on some systems, which we don't want
  char buf[512];
  JASSERT(::gethostname(buf, sizeof(buf))==0)(JASSERT_ERRNO);
  //so return a bad hash of our hostname
  long h = 0;
  for(char* i=buf; *i!='\0'; ++i)
    h = (*i) + (331*h);
  //make it positive for good measure
  return h>0 ? h : -1*h;
#endif
}

static char _prefix[32];
static void setPrefix()
{
  memset(_prefix, 0, sizeof(_prefix));
  if (getenv(ENV_VAR_PREFIX_ID) != NULL) {
    strncpy(_prefix, getenv(ENV_VAR_PREFIX_ID), sizeof(_prefix) - 1);
  }
}


static dmtcp::UniquePid& nullProcess()
{
  static char buf[sizeof(dmtcp::UniquePid)];
  static dmtcp::UniquePid* t=NULL;
  if(t==NULL) t = new (buf) dmtcp::UniquePid(0,0,0);
  return *t;
}
static dmtcp::UniquePid& theProcess()
{
  static char buf[sizeof(dmtcp::UniquePid)];
  static dmtcp::UniquePid* t=NULL;
  if(t==NULL) t = new (buf) dmtcp::UniquePid(0,0,0);
  return *t;
}
static dmtcp::UniquePid& parentProcess()
{
  static char buf[sizeof(dmtcp::UniquePid)];
  static dmtcp::UniquePid* t=NULL;
  if(t==NULL) t = new (buf) dmtcp::UniquePid(0,0,0);
  return *t;
}

dmtcp::UniquePid::UniquePid(const char *filename)
{
  char *str = strdup(filename);
  dmtcp::vector<char *> tokens;
  char *token = strtok(str, "_");
  while (token != NULL) {
    tokens.push_back(token);
    token = strtok(NULL, "_");
  } while (token != NULL);
  JASSERT(tokens.size() >= 3);

  char *uidstr = tokens.back();
  char *hostid_str = strtok(uidstr, "-");
  char *pid_str = strtok(NULL, "-");
  char *time_str = strtok(NULL, ".");

  _hostid = strtoll(hostid_str, NULL, 16);
  _pid = strtol(pid_str, NULL, 10);
  _time = strtol(time_str, NULL, 16);
  _generation = 0;
  memset(_prefix, 0, sizeof _prefix);
}

// _generation field of return value may later have to be modified.
// So, it can't return a const dmtcp::UniquePid
dmtcp::UniquePid& dmtcp::UniquePid::ThisProcess(bool disableJTrace /*=false*/)
{
  if ( theProcess() == nullProcess() )
  {
    theProcess() = dmtcp::UniquePid ( theUniqueHostId() ,
                                      ::getpid(),
                                      ::time(NULL) );
    if (disableJTrace == false) {
      JTRACE ( "recalculated process UniquePid..." ) ( theProcess() );
    }
    setPrefix();
  }

  return theProcess();
}

dmtcp::UniquePid& dmtcp::UniquePid::ParentProcess()
{
  return parentProcess();
}

/*!
    \fn dmtcp::UniquePid::UniquePid()
 */
dmtcp::UniquePid::UniquePid()
{
  _pid = 0;
  _hostid = 0;
  memset(&_time, 0, sizeof(_time));
}

void  dmtcp::UniquePid::incrementGeneration()
{
  _generation++;
}

const char* dmtcp::UniquePid::getCkptFilename()
{
  if (_ckptFileName().empty()) {
    dmtcp::ostringstream o;
    o << getCkptDir() << "/"
      << CKPT_FILE_PREFIX
      << jalib::Filesystem::GetProgramName()
      << '_' << _prefix << ThisProcess()
      << CKPT_FILE_SUFFIX;

    _ckptFileName() = o.str();
  }
  return _ckptFileName().c_str();
}

dmtcp::string dmtcp::UniquePid::getCkptFilesSubDir()
{
  if (_ckptFilesSubDir().empty()) {
    dmtcp::ostringstream o;
    o << getCkptDir() << "/"
      << CKPT_FILE_PREFIX
      << jalib::Filesystem::GetProgramName()
      << '_' << _prefix << ThisProcess()
      << CKPT_FILES_SUBDIR_SUFFIX;

    _ckptFilesSubDir() = o.str();
  }
  return _ckptFilesSubDir();
}

void dmtcp::UniquePid::createCkptDir()
{
  updateCkptDir();
  dmtcp::string dirname = _ckptDir() + _uniqueDir();
  JASSERT(mkdir(dirname.c_str(), S_IRWXU) == 0 || errno == EEXIST)
    (JASSERT_ERRNO) (dirname)
    .Text("Error creating checkpoint directory");

  JASSERT(0 == access(dirname.c_str(), X_OK|W_OK)) (dirname)
    .Text("ERROR: Missing execute- or write-access to checkpoint dir");
}

dmtcp::string dmtcp::UniquePid::getCkptDir()
{
  if (_ckptDir().empty()) {
    updateCkptDir();
  }
  JASSERT(!_ckptDir().empty());
  return _ckptDir() + _uniqueDir();
}

void dmtcp::UniquePid::setCkptDir(const char *dir)
{
  JASSERT(dir != NULL);
  _ckptDir() = dir;
  _ckptFileName().clear();
  _ckptFilesSubDir().clear();

  JASSERT(access(_ckptDir().c_str(), X_OK|W_OK) == 0) (_ckptDir())
    .Text("Missing execute- or write-access to checkpoint dir.");
}

void dmtcp::UniquePid::updateCkptDir()
{
  _ckptFileName().clear();
  _ckptFilesSubDir().clear();
  if (_ckptDir().empty()) {
    const char *dir = getenv(ENV_VAR_CHECKPOINT_DIR);
    if (dir == NULL) {
      dir = ".";
    }
    setCkptDir(dir);
  }

#ifdef UNIQUE_CHECKPOINT_FILENAMES
  UniquePid compId(SharedData::getCompId());
  JASSERT(compId != UniquePid(0,0,0));
  JASSERT(compId.generation() != -1);

  dmtcp::ostringstream o;
  o << "/ckpt_" << _prefix << compId << "_"
    << std::setw(5) << std::setfill('0') << compId.generation();
  _uniqueDir() = o.str();
#endif
}

dmtcp::string dmtcp::UniquePid::dmtcpTableFilename()
{
  static int count = 0;
  dmtcp::ostringstream os;

  os << getTmpDir() << "/dmtcpConTable." << _prefix << ThisProcess()
     << '_' << jalib::XToString ( count++ );
  return os.str();
}

dmtcp::string dmtcp::UniquePid::pidTableFilename()
{
  static int count = 0;
  dmtcp::ostringstream os;

  os << getTmpDir() << "/dmtcpPidTable." << _prefix << ThisProcess()
     << '_' << jalib::XToString ( count++ );
  return os.str();
}

#ifdef RUN_AS_ROOT
/* Global variable stores the name of the tmp directory when setTmpDir() is
 * called.
 */
string g_tmpDirName = "";
#endif

dmtcp::string dmtcp::UniquePid::getTmpDir()
{
  dmtcp::string device = jalib::Filesystem::ResolveSymlink ( "/proc/self/fd/"
                           + jalib::XToString ( PROTECTED_TMPDIR_FD ) );
  if ( device.empty() ) {
    JWARNING ( false ) .Text ("Unable to determine DMTCP_TMPDIR, retrying.");
    setTmpDir(getenv(ENV_VAR_TMPDIR));
    device = jalib::Filesystem::ResolveSymlink ( "/proc/self/fd/"
               + jalib::XToString ( PROTECTED_TMPDIR_FD ) );
#ifndef RUN_AS_ROOT
    JASSERT ( !device.empty() )
      .Text ( "Still unable to determine DMTCP_TMPDIR" );
#else
    /* For an application that gives up its privileges after
     * starting as root (using setuid() for example), the checkpoint
     * thread will not be able to open up /proc/self/fd/. This is a
     * temporary fix for this problem.
     */
    JASSERT (PROTECTED_TMPDIR_FD)
      .Text ( "Unable to determine DMTCP_TMPDIR. Setting it to default value." );
    /* We return a sane value now. This is in addition to the previous fix
     * (r2242).
     */
    device =  g_tmpDirName;
#endif
  }
  return device;
}


/*
 * setTmpDir() computes the TmpDir to be used by DMTCP. It does so by using
 * DMTCP_TMPDIR env, current username, and hostname. Once computed, we open the
 * directory on file descriptor PROTECTED_TMPDIR_FD. The getTmpDir() routine
 * finds the TmpDir from looking at PROTECTED_TMPDIR_FD in proc file system.
 *
 * This mechanism was introduced to avoid calls to gethostname(), getpwuid()
 * etc. while DmtcpWorker was still initializing (in constructor) or the
 * process was restarting. gethostname(), getpwuid() will create a socket
 * connect to some DNS server to find out hostname and username. The socket is
 * closed only at next exec() and thus it leaves a dangling socket in the
 * worker process. To resolve this issue, we make sure to call setTmpDir() only
 * from dmtcp_launch and dmtcp_restart process and once the user process
 * has been exec()ed, we use getTmpDir() only.
 */
void dmtcp::UniquePid::setTmpDir(const char* envVarTmpDir) {
  dmtcp::string tmpDir;

  char hostname[256];
  memset(hostname, 0, sizeof(hostname));

  JASSERT ( gethostname(hostname, sizeof(hostname)) == 0 ||
	    errno == ENAMETOOLONG ).Text ( "gethostname() failed" );

  dmtcp::ostringstream o;

  char *userName = const_cast<char *>("");
  if ( getpwuid ( getuid() ) != NULL ) {
    userName = getpwuid ( getuid() ) -> pw_name;
  } else if ( getenv("USER") != NULL ) {
    userName = getenv("USER");
  }

  if (envVarTmpDir) {
    o << envVarTmpDir;
  } else if (getenv("TMPDIR")) {
    o << getenv("TMPDIR") << "/dmtcp-" << userName << "@" << hostname;
  } else {
    o << "/tmp/dmtcp-" << userName << "@" << hostname;
  }

  JASSERT(mkdir(o.str().c_str(), S_IRWXU) == 0 || errno == EEXIST)
    (JASSERT_ERRNO) (o.str())
    .Text("Error creating tmp directory");

  JASSERT(0 == access(o.str().c_str(), X_OK|W_OK)) (o.str())
    .Text("ERROR: Missing execute- or write-access to tmp dir");

  int tmpFd = open ( o.str().c_str(), O_RDONLY  );
  JASSERT(tmpFd != -1);
  JASSERT(_real_dup2(tmpFd, PROTECTED_TMPDIR_FD)==PROTECTED_TMPDIR_FD);

#ifdef RUN_AS_ROOT
  /* This is a temporary fix for the double-restart (ckpt->rst->ckpt->rst)
   * problem with Apache. We save the path of the tmp directory here in
   * a global variable that is referred to later when getTmpDir() is called
   * on restart.
   */
  g_tmpDirName = o.str();
#endif

  close ( tmpFd );
}

void dmtcp::UniquePid::restart()
{
  string ckptDir = jalib::Filesystem::GetDeviceName(PROTECTED_CKPT_DIR_FD);
  JASSERT(ckptDir.length() > 0);
  _real_close(PROTECTED_CKPT_DIR_FD);
  setCkptDir(ckptDir.c_str());
}

/*!
    \fn dmtcp::UniquePid::operator<() const
 */
bool dmtcp::UniquePid::operator< ( const UniquePid& that ) const
{
#define TRY_LEQ(param) if(this->param != that.param) return this->param < that.param;
  TRY_LEQ ( _hostid );
  TRY_LEQ ( _pid );
  TRY_LEQ ( _time );
  return false;
}

bool dmtcp::UniquePid::operator== ( const UniquePid& that ) const
{
  return _hostid==that.hostid()
         && _pid==that.pid()
         && _time==that.time();
         // FIXME: Reinstate prefix check
         //&& strncmp(_prefix, that.prefix(), sizeof(_prefix)) == 0;
}

dmtcp::ostream& dmtcp::operator<< ( dmtcp::ostream& o,const dmtcp::UniquePid& id )
{
  o << std::hex << id.hostid() << '-' << std::dec << id.pid() << '-' << std::hex << id.time() << std::dec;
  return o;
}

dmtcp::ostream& dmtcp::operator<< ( dmtcp::ostream& o,const DmtcpUniqueProcessId& id )
{
  o << std::hex << id._hostid<< '-' << std::dec << id._pid << '-' << std::hex << id._time << std::dec;
  return o;
}

bool dmtcp::operator==(const DmtcpUniqueProcessId& a,
                       const DmtcpUniqueProcessId& b)
{
  return a._hostid == b._hostid &&
         a._pid == b._pid &&
         a._time == b._time &&
         a._generation == b._generation;
}

bool dmtcp::operator!=(const DmtcpUniqueProcessId& a,
                       const DmtcpUniqueProcessId& b)
{
  return !(a == b);
}

dmtcp::string dmtcp::UniquePid::toString() const{
  dmtcp::ostringstream o;
  o << *this;
  return o.str();
}

void dmtcp::UniquePid::resetOnFork ( const dmtcp::UniquePid& newId )
{
  // parentProcess() is for inspection tools
  parentProcess() = ThisProcess();
  JTRACE ( "Explicitly setting process UniquePid" ) ( newId );
  theProcess() = newId;
  _ckptFileName().clear();
  _ckptFilesSubDir().clear();
  //_ckptDir().clear();
}

bool dmtcp::UniquePid::isNull() const
{
  return (*this == nullProcess());
}

void dmtcp::UniquePid::serialize ( jalib::JBinarySerializer& o )
{
  // NOTE: Do not put JTRACE/JNOTE/JASSERT in here
  UniquePid theCurrentProcess, theParentProcess;

  if ( o.isWriter() )
  {
    theCurrentProcess = ThisProcess();
    theParentProcess = ParentProcess();
  }

  o & theCurrentProcess & theParentProcess;

  if ( o.isReader() )
  {
    theProcess() = theCurrentProcess;
    parentProcess() = theParentProcess;
  }
}
