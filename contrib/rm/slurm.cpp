/****************************************************************************
 *  Copyright (C) 2012-2013 by Artem Y. Polyakov <artpol84@gmail.com>       *
 *                                                                          *
 *  This file is part of the RM plugin for DMTCP                        *
 *                                                                          *
 *  RM plugin is free software: you can redistribute it and/or          *
 *  modify it under the terms of the GNU Lesser General Public License as   *
 *  published by the Free Software Foundation, either version 3 of the      *
 *  License, or (at your option) any later version.                         *
 *                                                                          *
 *  RM plugin is distributed in the hope that it will be useful,        *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 *  GNU Lesser General Public License for more details.                     *
 *                                                                          *
 *  You should have received a copy of the GNU Lesser General Public        *
 *  License along with DMTCP:dmtcp/src.  If not, see                        *
 *  <http://www.gnu.org/licenses/>.                                         *
 ****************************************************************************/

/* SLURM resource manager supporting code
*/

#include <stdlib.h>
#include <linux/limits.h>
#include <pthread.h>
#include <vector>
#include <list>
#include <string>
#include <util.h>
#include <jalib.h>
#include <jassert.h>
#include <jconvert.h>
#include <jfilesystem.h>
#include <dmtcpplugin.h>
#include "resource_manager.h"
#include "slurm.h"


void probeSlurm()
{
  JTRACE("Start");
  if( (getenv("SLURM_JOBID") != NULL) && (NULL != getenv("SLURM_NODELIST")) ){
    JTRACE("We run under SLURM!");
    // TODO: Do we need locking here?
    //JASSERT(_real_pthread_mutex_lock(&global_mutex) == 0);
    _set_rmgr_type(slurm);
  }
}



static void print_args(char *const argv[])
{
  int i;
  dmtcp::string cmdline;
  for (i = 0; argv[i] != NULL; i++ ) {
    cmdline +=  dmtcp::string() + argv[i] + " ";
  }

  JTRACE("Init CMD:")(cmdline);
}

static int patch_srun_cmdline(char * const argv_old[], char ***_argv_new)
{
  // Calculate initial argc
  int argc_old;
  for(argc_old=0; argv_old[argc_old] != NULL; argc_old++);
  argc_old++;

  // Prepare DMTCP part of exec* string
  char dmtcpCkptPath[PATH_MAX] = "dmtcp_launch";
  int ret = 0;
//  dmtcp::string ckptCmdPath = dmtcp::Util::getPath("dmtcp_launch");
//  int ret = dmtcp::Util::expandPathname(ckptCmdPath.c_str(),
//                                    dmtcpCkptPath, sizeof(dmtcpCkptPath));

  JTRACE("Expand dmtcp_launch path")(dmtcpCkptPath);

  dmtcp::vector<dmtcp::string> dmtcp_args;
  dmtcp::Util::getDmtcpArgs(dmtcp_args);
  unsigned int dsize = dmtcp_args.size();
  
  // Prepare final comman line
  *_argv_new = new char *[argc_old + (dsize + 1)]; // (dsize+1) is DMTCP part including dmtcpCkptPath
  char **argv_new = *_argv_new;
  
  
  // Move srun part like: srun --nodes=3 --ntasks=3 --kill-on-bad-exit --nodelist=c2909,c2911,c2913 
  // first string is srun and we move it anyway
  // all srun options starts with one or two dashes so we copy until see '-'.
  argv_new[0] = argv_old[0];
  size_t i;
  for(i=1; i < argc_old && argv_old[i][0] == '-'; i++){
    argv_new[i] = argv_old[i];
  }
  int old_pos = i;
  int new_pos = i;
  
  // Copy dmtcp part so final command looks like: srun <opts> dmtcp_launch <dmtcp_options> orted <orted_options>
  argv_new[new_pos] = strdup(dmtcpCkptPath);
  new_pos++;
  for (i = 0; i < dsize; i++, new_pos++) {
    argv_new[new_pos] = strdup(dmtcp_args[i].c_str());
  }
  
  for (; old_pos < argc_old; old_pos++, new_pos++) {
    argv_new[new_pos] = argv_old[old_pos];
  }
  
  dmtcp::string cmdline;
  for (int i = 0; argv_new[i] != NULL; i++ ) {
    cmdline +=  dmtcp::string() + argv_new[i] + " ";
  }

  JTRACE( "call SLURM srun to run command on remote host" )
        ( argv_new[0] );
  JTRACE("CMD:")(cmdline);
  return ret;
}

void close_all_fds()
{
  jalib::IntVector fds =  jalib::Filesystem::ListOpenFds();
  for(int i = 0 ; i < fds.size(); i++){
    JTRACE("fds")(i)(fds[i]);
    if( fds[i] > 2 ){ 
      JTRACE("Close")(i)(fds[i]);
      jalib::close(fds[i]);
    }
  }
  fds =  jalib::Filesystem::ListOpenFds();
  JTRACE("After close:");
  for(int i = 0 ; i < fds.size(); i++){
    JTRACE("fds")(i)(fds[i]);
  }
  
}

extern "C" int execve (const char *filename, char *const argv[],
                       char *const envp[])
{
  if (jalib::Filesystem::BaseName(filename) != "srun") {
    return _real_execve(filename, argv, envp);
  }

  print_args(argv);
  char **argv_new;
  patch_srun_cmdline(argv, &argv_new);
  
  dmtcp::string cmdline;
  for (int i = 0; argv_new[i] != NULL; i++ ) {
    cmdline +=  dmtcp::string() + argv_new[i] + " ";
  }
  JTRACE( "How command looks from exec*:" );
  JTRACE("CMD:")(cmdline);
  JTRACE("envp:");
  for(int i = 0; envp[i] != NULL; i++){
    JTRACE("envp[i]")(i)(envp[i]);
  }
  
  close_all_fds();
  
  return _real_execve(filename, argv_new, envp);
}

extern "C" int execvp (const char *filename, char *const argv[])
{
  if (jalib::Filesystem::BaseName(filename) != "srun") {
    return _real_execvp(filename, argv);
  }

  print_args(argv);

  char **argv_new;
  patch_srun_cmdline(argv, &argv_new);

  dmtcp::string cmdline;
  for (int i = 0; argv_new[i] != NULL; i++ ) {
    cmdline +=  dmtcp::string() + argv_new[i] + " ";
  }

  JTRACE( "How command looks from exec*:" );
  JTRACE("CMD:")(cmdline);

  close_all_fds();

  return _real_execvp(filename, argv_new);
}

// This function first appeared in glibc 2.11
extern "C" int execvpe (const char *filename, char *const argv[],
                         char *const envp[])
{
  if (jalib::Filesystem::BaseName(filename) != "srun") {
    return _real_execvpe(filename, argv, envp);
  }

  print_args(argv);

  char **argv_new;
  patch_srun_cmdline(argv, &argv_new);

  dmtcp::string cmdline;
  for (int i = 0; argv_new[i] != NULL; i++ ) {
    cmdline +=  dmtcp::string() + argv_new[i] + " ";
  }
  JTRACE( "How command looks from exec*:" );
  JTRACE("CMD:")(cmdline);

  close_all_fds();

  return _real_execvpe(filename, argv_new, envp);
}