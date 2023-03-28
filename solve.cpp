#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <iostream>
#include <vector>
#include <queue>
#include <cassert>
#include <chrono>

// TODO: move to arguments (maybe ratio?)
#define N_WORKERS 	2 // Skrzaty
#define N_KILLERS 	0 // Gnomy
#define N_PINS 		1 // Agrafki
#define N_SCOPES 	0 // Celowniki
					  // 
#define GNOME_MESSAGE_TAG 123
		
enum {
	MESSAGE_TYPE_REQUEST = 1,
	MESSAGE_TYPE_ACK,
	MESSAGE_TYPE_RELEASE,
};

enum { 
	RESOURCE_TYPE_PIN_SCOPE = 0,
	// RESOURCE_TYPE_SCOPE,
	RESOURCE_TYPE_GUN,
	RESOURCE_TYPE_N
};

enum {
	GNOME_STATE_RESTING = 0,
	GNOME_STATE_REQUESTING, 
};

enum {
	GNOME_TYPE_NONE = 0,
	GNOME_TYPE_WORKER = 100,
	GNOME_TYPE_KILLER = 200,
};

struct GnomeMessage {
	int message_type;
	int resource_type; 
	int lamport_timestamp;
	int proc_id;
	bool sent_ack; // did we already sent OK to process?
};

struct GnomeMessageComparator {
public:
    bool operator() (const GnomeMessage& lhs, const GnomeMessage& rhs) {
		if (lhs.lamport_timestamp != rhs.lamport_timestamp) return lhs.lamport_timestamp < rhs.lamport_timestamp;
		else return lhs.proc_id < rhs.proc_id;
    }
};

class Gnome {
	int tid = -1;
	int type = GNOME_TYPE_NONE; 
	int state = GNOME_STATE_RESTING;
	int lamport = 0; 

	std::vector<bool> ack_from_same_type;
	std::vector<int> same_type_ids;
	std::vector<int> other_type_ids;
	std::vector<int> resources;

	std::priority_queue< GnomeMessage, std::vector<GnomeMessage>, GnomeMessageComparator > queue;
	
public:
	Gnome(int g_tid, int g_type, const std::vector<int>& workers, const std::vector<int>& killers, const std::vector<int>& res_counts) {
		tid = g_tid;
		type = g_type;
		state = GNOME_STATE_RESTING;
		lamport = 0;
		resources = res_counts;
		
		if (type == GNOME_TYPE_WORKER) {
			same_type_ids = workers;
			other_type_ids = killers;
			ack_from_same_type.assign(workers.size(), false);
		} else if (type == GNOME_TYPE_KILLER) {
			same_type_ids = killers;
			other_type_ids = workers;
			ack_from_same_type.assign(killers.size(), false);
		}
	}
	
	void perform(bool requester = false, float req_interval_s = 5.0f) {
		if (type == GNOME_TYPE_NONE) {
			std::cout << "just none type, returning..." << std::endl;
		}
		
		std::cout << "Starting as requester: " << requester << std::endl;
		auto last_start = std::chrono::system_clock::now();

		while (true) {
			auto end = std::chrono::system_clock::now();
			auto elapsed = std::chrono::duration<float>(end - last_start);
			float seconds = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();


			if (requester && seconds > req_interval_s) {
				request_resource(RESOURCE_TYPE_PIN_SCOPE);
				std::cout << "REQUESTING!" << std::endl;
				last_start = std::chrono::system_clock::now();
			}

			MPI_Status status;
			int probe_flag;
			int probe_ret = MPI_Iprobe(MPI_ANY_SOURCE, GNOME_MESSAGE_TAG, MPI_COMM_WORLD, &probe_flag, &status);
			
			if (!probe_flag) continue;


			std::cout << "MESSAGE ARRIVED! from: " << status.MPI_SOURCE << std::endl;

			int buffer[3]; // type, timestamp, resource
			MPI_Recv(buffer, 3, MPI_INT, MPI_ANY_SOURCE, GNOME_MESSAGE_TAG, MPI_COMM_WORLD, &status);
			GnomeMessage gm = { buffer[0], buffer[1], buffer[2], status.MPI_SOURCE, false };
			react_to_message(gm);
			
		}
	}
	
private:

	void react_to_message(GnomeMessage gm) {
		// TODO: check if needed
		int old_lamport = lamport;
		lamport = std::max(gm.lamport_timestamp, lamport) + 1;

		switch (gm.message_type) {
			case MESSAGE_TYPE_REQUEST: {
				send_ack(gm.proc_id, gm.resource_type);
			} break;
			case MESSAGE_TYPE_RELEASE: {
				assert(false);
			} break;
			case MESSAGE_TYPE_ACK: {
				std::cout << "I RECEIVED ACK!!!" << std::endl;
			} break;
		}
		// TODO: Validate
	} 
	
	void send_ack(int gnome_id, int resource) {
		lamport++;
		int buffer[3] = { MESSAGE_TYPE_ACK, resource, lamport };
		MPI_Send(buffer, 3, MPI_INT, gnome_id, GNOME_MESSAGE_TAG, MPI_COMM_WORLD);
	}
	
	void request_resource(int resource) {
		// If I am worker/killer -> broadcast message to all workers/killers that I want to take resource
		lamport++;
		int buffer[3] = { MESSAGE_TYPE_REQUEST, resource, lamport };
		for (auto gnome_id : same_type_ids) {
			MPI_Send(buffer, 3, MPI_INT, gnome_id, GNOME_MESSAGE_TAG, MPI_COMM_WORLD);
		}
	}

	void release_resource(int resource) {
		// If I am worker/killer -> broadcast message to all killers/workers that I want released resource
		lamport++;
		int buffer[3] = { MESSAGE_TYPE_RELEASE, resource, lamport };
		for (auto gnome_id : other_type_ids) {
			MPI_Send(buffer, 3, MPI_INT, gnome_id, GNOME_MESSAGE_TAG, MPI_COMM_WORLD);
		}
	}
};


int assign_gnome_roles(std::vector<int>& workers, std::vector<int>& killers, int n_workers, int n_killers, int my_tid) {
	// Return my Gnome Role 
	// tid in [0 : n_workers) -> gnome worker
	// tid in [n_workers : n_workers + n_killers) -> gnome killer
	// tid in [n_workers + n_killers : ...)  -> none
	// + fill vectors with ids of other gnomes
	
	for (int i = 0; i < n_workers; i++) {
		if (i != my_tid) workers.push_back(i);
	}
	for (int i = n_workers; i < n_workers + n_killers; i++) {
		if (i != my_tid) killers.push_back(i);
	}

	if (my_tid < n_workers) {
		return GNOME_TYPE_WORKER;
	} else if (my_tid < n_workers + n_killers) {
		return GNOME_TYPE_KILLER;
	} else {
		return GNOME_TYPE_NONE;
	}

	return (my_tid < n_workers ? GNOME_TYPE_WORKER : GNOME_TYPE_KILLER);
}

int main(int argc, char **argv) {
	MPI_Status status;
	int tid, size;
	int provided;

	MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided); //Musi być w każdym programie na początku
	MPI_Comm_size( MPI_COMM_WORLD, &size );
	MPI_Comm_rank( MPI_COMM_WORLD, &tid );

	printf("My id is %d from %d\n",tid, size);
	
	// We can pass maximums for both workers and killers
	// - if possible we try to get all workers
	// - we try to assign what left to killers
	int n_workers = std::min(N_WORKERS, size);
	int n_killers = std::min(size - n_workers, N_KILLERS);
	
	std::vector<int> workers;
	std::vector<int> killers;
	int my_type = assign_gnome_roles(workers, killers, n_workers, n_killers, tid);
	

	std::vector<int> resources { N_SCOPES, 0 };
	
	Gnome gnome(tid, my_type, workers, killers, resources);
	if (tid == 0) {
		gnome.perform(true);
	} else {
		gnome.perform();
	}

	MPI_Finalize(); // Musi być w każdym programie na końcu
}
