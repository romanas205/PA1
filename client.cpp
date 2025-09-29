/*
   Original author of the starter code
   Tanzir Ahmed
   Department of Computer Science & Engineering
   Texas A&M University
   Date: 2/8/20
  
   Please include your Name, UIN, and the date below
   Name: Roman Nasyrov
   UIN: 234005009
   Date: September 27, 2025
*/
#include "common.h"
#include "FIFORequestChannel.h"

// system headers for fork, exec, wait, file I/O
#include <sys/wait.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <cmath> // For round

using namespace std;

int p = 0;
double t = -1.0;
int e = 0;
string filename = "";
int c = 0;
int m = MAX_MESSAGE;

// function to process a single data point request
void process_single_data_request(FIFORequestChannel* chan) {
    if (p > 0 && t >= 0.0 && e > 0) {
        char buf[MAX_MESSAGE];
        datamsg x(p, t, e);
      
        memcpy(buf, &x, sizeof(datamsg));
        chan->cwrite(buf, sizeof(datamsg)); // question
        
        double reply;
        chan->cread(&reply, sizeof(double)); // answer
        cout << "For person " << p << ", at time " << t << ", the value of ecg " << e << " is " << reply << endl;
    }
}

// process multiple data point requests for x1.csv
void process_multi_data_request(FIFORequestChannel* chan) {
    if (p > 0 && t < 0.0 && e == 0 && filename == "") {
        // -p <patient number> case for x1.csv
        string output_filename = "received/x1.csv"; 
        FILE* fp = fopen(output_filename.c_str(), "w");

        // indices 0 through 999 is 1000 data points
        for (int i = 0; i < 1000; ++i) {
            double time_to_request = i * 0.004;

            // Request ECG 1
            char buf1[MAX_MESSAGE];
            datamsg x1(p, time_to_request, 1);
            memcpy(buf1, &x1, sizeof(datamsg));
            chan->cwrite(buf1, sizeof(datamsg));
            
            double reply1;
            chan->cread(&reply1, sizeof(double));

            // Request ECG 2
            char buf2[MAX_MESSAGE];
            datamsg x2(p, time_to_request, 2);
            memcpy(buf2, &x2, sizeof(datamsg));
            chan->cwrite(buf2, sizeof(datamsg));

            double reply2;
            chan->cread(&reply2, sizeof(double));

            // replaced precision to just %g
            fprintf(fp, "%g,%g,%g\n", time_to_request, reply1, reply2);
        }

        fclose(fp);
    }
}

// process a file request
void process_file_request(FIFORequestChannel* chan) {
    if (filename != "") {
        string output_filename = "received/" + filename;
        FILE* fp = fopen(output_filename.c_str(), "wb"); // 'wb' for binary write

        // request file size (offset=0, length=0)
        filemsg fs_req(0, 0);
        int len = sizeof(filemsg) + filename.size() + 1;
        char* buf = new char[len];
        memcpy(buf, &fs_req, sizeof(filemsg));
        strcpy(buf + sizeof(filemsg), filename.c_str());
        chan->cwrite(buf, len);

        __int64_t file_size;
        chan->cread(&file_size, sizeof(__int64_t));
        delete[] buf; // Free buffer after use

        // request file content in chunks
        char* recv_buffer = new char[m];
        __int64_t current_offset = 0;

        while (current_offset < file_size) {
            int request_length = (int) min((__int64_t)m, file_size - current_offset); // find smaller value and cast to int to avoid errors

            filemsg data_req(current_offset, request_length);
            len = sizeof(filemsg) + filename.size() + 1;
            char* req_buf = new char[len];
            memcpy(req_buf, &data_req, sizeof(filemsg));
            strcpy(req_buf + sizeof(filemsg), filename.c_str());
            
            chan->cwrite(req_buf, len);
            delete[] req_buf;

            int bytes_read = chan->cread(recv_buffer, request_length);

            if (bytes_read > 0) {
                // fwrite for binary data, which is needed for non-text files
                fwrite(recv_buffer, 1, bytes_read, fp);
                current_offset += bytes_read;
            }
        }

        delete[] recv_buffer;
        fclose(fp); // transfer complete
    }
}


// make function to rocess all client requests
void process_client_requests(FIFORequestChannel* chan) {
    // determine the type of request and call needed function
    if (filename != "") {
        process_file_request(chan);
    } else if (p > 0 && t >= 0.0 && e > 0) {
        process_single_data_request(chan);
    } else if (p > 0 && t < 0.0 && e == 0) {
        process_multi_data_request(chan);
    }
    // the program will terminate after cleaning up if flags are not specified
}


int main (int argc, char *argv[]) {
    int opt;
    // c has no argument, m p t e f all require argument
    while ((opt = getopt(argc, argv, "c::m:p:t:e:f:")) != -1) {
        switch (opt) {
            case 'c':
                c = 1; // New channel flag
                break;
            case 'm':
                m = atoi(optarg); // Buffer capacity
                break;
            case 'p':
                p = atoi (optarg);
                break;
            case 't':
                t = atof (optarg);
                break;
            case 'e':
                e = atoi (optarg);
                break;
            case 'f':
                filename = optarg;
                break;
        }
    }
    
    pid_t pid = fork();

    if (pid == 0) { 
		// Child process = server
        char* server_argv[4];
		// cast to char* to match array
        server_argv[0] = (char*)"./server"; // the first arg is the name of the script
        server_argv[1] = (char*)"-m";
        // The buffer capacity must be passed as a string
        string m_str = to_string(m);
        server_argv[2] = (char*)m_str.c_str();
        server_argv[3] = NULL; // null termination at the end
        
        execvp(server_argv[0], server_argv);
    }

    // parent continues here
	// removed non-sense data related code
    FIFORequestChannel control_chan("control", FIFORequestChannel::CLIENT_SIDE);

    FIFORequestChannel* current_chan = &control_chan;

    // check if new channel was requested
    if (c) {
        MESSAGE_TYPE nc_msg = NEWCHANNEL_MSG;
        current_chan->cwrite(&nc_msg, sizeof(MESSAGE_TYPE));
        
        char channel_name_buf[MAX_MESSAGE];
        current_chan->cread(channel_name_buf, MAX_MESSAGE);
        
        FIFORequestChannel* new_chan = new FIFORequestChannel(channel_name_buf, FIFORequestChannel::CLIENT_SIDE);
        current_chan = new_chan;
    }

    process_client_requests(current_chan);

    
    // Send QUIT_MSG to the active channel to close it
    MESSAGE_TYPE quit_msg = QUIT_MSG;
    current_chan->cwrite(&quit_msg, sizeof(MESSAGE_TYPE));
    
    // If a new channel was used, delete it
    if (current_chan != &control_chan) {
        delete current_chan;
    }

    // Send QUIT_MSG to the control channel (which is always open)
    MESSAGE_TYPE quit_control_msg = QUIT_MSG;
    control_chan.cwrite(&quit_control_msg, sizeof(MESSAGE_TYPE));

    // Wait for the server to terminat
    int status;
    wait(&status);

    return 0;
}