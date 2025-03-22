 /*
 * POpen.hpp
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
 * Copyright (c) 2013, 2014, 2015 Frank C. Earl
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

#ifndef POPEN_H_
#define POPEN_H_

#include <string>
using std::string;

class POpen
{
public:
	POpen() { init_process_values(); };
	POpen(string command) { run_command(command); };
	virtual ~POpen();

	// Execution/control methods...
	int run_command(string command);
	int close(void);
	int kill(void);
	int terminate(void);
	bool isRunning(void);

	// Get handle methods - this allows you the ability to supply data to
	// and get data from the child process' stdin/stdout.  (If you don't
	// need bidirectional action or C++ semantics/operation, then popen()
	// or system() is probably a better bet for things here...)
	int getReadFd(void) { return _readFd; };
	int getWriteFd(void) { return _writeFd; };
	FILE* getReadfFp(void) { return _readFp; };
	FILE* getWriteFp(void) { return _writeFp; };

private:
	// All of our POpen process info...
	pid_t	 _pid;
    FILE 	 *_readFp;
    FILE 	 *_writeFp;
    int 	 _readFd;
    int 	 _writeFd;
    char 	 _sys_cmd[64];

    // Some internal-only definitions...
	const int READ = 0;
	const int WRITE = 1;

	void reset(void);
    void init_process_values(void);
    void close_pipe(int *pipeset);
};

#endif /* POPEN_H_ */
