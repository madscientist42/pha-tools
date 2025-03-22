 /*
 * POpen.cpp
 *
 * An abstracted dual-pipe popen() equivalent for C++ to use.  Doesn't (yet)
 * provide iostreams like C++ operator support for things (you'll have to
 * use either a C FILE* or a POSIX file descriptor and the operations there
 * to fully use this.  If you're needing to just simply call a shell driven
 * command to do work, or if you can just use one inbound or outbound pipe,
 * system() or popen() might be a better choice without the stream I/O
 * abstractions in place.  This was developed to allow me to launch a command
 * and expose the full console as a gateway to the inner child application
 * through console/telnet/ssh support.
 *
 * This *requires* a 2011 C++ standard compliant compiler to compile and work.
 *
 * Copyright (c) 2013, 2014, 2015, 2018 Frank C. Earl
 * All Rights Reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions, and the following disclaimer.  You may add your
 *    own copyright notice relative to your modifications, but you cannot claim
 *    the code herein as solely your own.
 *
 * 2. Binary redistributions must reproduce the above copyright notice, either
 *    in the initial output of the derived application, a "help" screen, or in
 *    the documentation that accompanies the same.
 *
 * 3. Neither the name of the copyright holder nor the names of this software's
 *    contributors may be used to endorse or promote products derived from this
 *    software without specific prior written permission.  Compliance with
 *    condition 2 does not constitute a violation of this condition.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * SPECIFICALLY DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <paths.h>
#include <sys/stat.h>
#include <dirent.h>
#include <stdlib.h>
#include <string.h>

#include <POpen.hpp>

/**
 * Executes a shell command in a child process, establishing dual-pipe
 * communication for input and output.
 *
 * This function sets up pipes for inter-process communication, forks a child
 * process, and executes the provided shell command in the child process. The
 * parent process can then communicate with the child using the established
 * pipes.
 *
 * @param command The shell command to be executed.
 * @return 0 on successful execution, -EIO on failure.
 *
 * Note: The function assumes that the command will be executed using the
 * system's default shell, typically /bin/sh.
 */

int POpen::run_command(string command)
{
	int		retVal = -EIO;
	int 	inpipe[2];
	int 	outpipe[2];
	char 	*argv[4];

	// Close out the previous process if we have one...
	reset();

	// Now, set things up...
	if (pipe(inpipe) == 0)
	{
		if (pipe(outpipe) == 0)
		{
			_readFp = fdopen(outpipe[READ], "r");
			if (_readFp != NULL)
			{
				_writeFp = fdopen(inpipe[WRITE], "w");
				if (_writeFp != NULL)
				{
					_readFd = outpipe[READ];
					_writeFd = inpipe[WRITE];
					_pid = fork();
					if (_pid != -1)
					{
						if (_pid == 0)
						{
							/* We're the child... */
							::close(outpipe[READ]);
							::close(inpipe[WRITE]);

							if (inpipe[READ] != STDIN_FILENO)
							{
								dup2(inpipe[READ], STDIN_FILENO);
								::close(inpipe[READ]);
							}

							if (outpipe[WRITE] != STDOUT_FILENO)
							{
								dup2(outpipe[WRITE], STDOUT_FILENO);
								::close(outpipe[WRITE]);
							}

							argv[0] = (char *) "sh";
							argv[1] = (char *) "-c";
							argv[2] = (char *) command.c_str();
							argv[3] = NULL;

							execv(_PATH_BSHELL, argv);
							exit(127);  // Child will die horribly if it gets here- as rightly it should.
						}

						retVal = 0;

						/* Close off the descriptors of the pipe ends the Child owns... */
						::close(outpipe[WRITE]);
						::close(inpipe[READ]);
					}
					else
					{
						fclose(_writeFp);
						fclose(_readFp);
						close_pipe(outpipe);
						close_pipe(inpipe);
					}
				}
				else
				{
					fclose(_readFp);
					close_pipe(outpipe);
					close_pipe(inpipe);
				}
			}
			else
			{
				close_pipe(outpipe);
				close_pipe(inpipe);
			}
		}
		else
		{
			close_pipe(inpipe);
		}
	}

	return retVal;
};


/**
 * Close this POpen object.
 *
 * This method waits for the subprocess to end and then releases any
 * resources associated with the object. If the subprocess is no longer
 * running, then the method returns immediately.
 *
 * @return 0 if successful, -1 if an error occurred.
 */
int POpen::close(void)
{
    int 	pstat;
    pid_t 	pid = -1;

    if (_pid != -1)
    {
        do
        {
            pid = ::waitpid(_pid, &pstat, 0);
        } while (pid == -1 && errno == EINTR);

    	init_process_values();
    }

    return (pid == -1 ? -1 : pstat);
}

/**
 * Reset this POpen object to its initial state.
 *
 * This method is used to reset this object to its initial state,
 * ready for a new call to open(). It will quietly fail if there
 * is no child process present.
 */
void POpen::reset(void)
{
	// Do a reap pass.  This will quietly fail if there's no
	// child process present...
	kill();
	close();
}

/**
 * Kill the Child process.
 *
 * This method kills the Child process using the kill(1) command with the
 * signal -9. This is a largely portable way to kill a process, but it
 * is not guaranteed to work on all platforms.
 *
 * @return 0 if the kill command was successful, otherwise non-zero.
 */
int POpen::kill(void)
{
	// Largely portable way to issue a kill to the Child.
	sprintf(_sys_cmd, "kill -9 %d", _pid);
	return system(_sys_cmd);
}

/**
 * Terminate the Child process.
 *
 * This method sends a SIGTERM signal to the Child process, requesting
 * it to terminate gracefully. The SIGTERM signal allows the process
 * to perform cleanup operations before shutting down. However, it is
 * not guaranteed that the process will terminate immediately, as it
 * may choose to ignore or handle the signal.
 *
 * @return 0 if the terminate command was successful, otherwise non-zero.
 */

int POpen::terminate(void)
{
	// Largely portable way to issue a kill to the Child.
	sprintf(_sys_cmd, "kill -TERM %d", _pid);
	return system(_sys_cmd);
}

/**
 * Check to see if the child process is running.
 *
 * This method checks to see if the child process is still running.
 * It does this by performing a waitpid() call with the WNOHANG flag
 * set, which allows it to check the status of the child process
 * without blocking. If the waitpid() call returns 0, then the child
 * process is still running. If it returns a non-zero value, then
 * the child process has terminated.
 *
 * @return true if the child process is running, false otherwise.
 */
bool POpen::isRunning(void)
{
	// Check to see if we know that the child process is running...
	int  status = 0;
	return (waitpid(_pid, &status, WNOHANG) == 0) ? true : false;
}

/**
 * Reset all the process related values to their initial state.
 *
 * This method closes all the file descriptors and file pointers associated
 * with the child process and resets all the member variables to their initial
 * state. It is used by the constructors and destructors of this class to
 * ensure that the class is always properly initialized.
 */
void POpen::init_process_values(void)
{
	if (_readFd > -1)
	{
		::close(_readFd);
	}
	if (_writeFd > -1)
	{
		::close(_writeFd);
	}
	if (_readFp != NULL)
	{
		fclose(_readFp);
	}
	if (_writeFp != NULL)
	{
		fclose(_writeFp);
	}
	_pid = _readFd = _writeFd = -1;
	_readFp = _writeFp = NULL;
}

/**
 * Close a pipe.
 *
 * This method takes a pointer to a set of two file descriptors that
 * represent a pipe and closes both of them.
 *
 * @param pipeset pointer to a set of two file descriptors.
 */
void POpen::close_pipe(int *pipeset)
{
	::close(pipeset[READ]);
	::close(pipeset[WRITE]);
};

