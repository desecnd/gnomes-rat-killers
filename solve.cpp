#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <iostream>
#include <vector>
#include <set>
#include <cassert>
#include <chrono>
#include <sstream>
#include <numeric>
#include <functional>

// TODO: move to arguments 
#define N_WORKERS 	2 // Skrzaty
#define N_KILLERS 	2 // Gnomy
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
	RESOURCE_TYPE_GUN = 1,
	RESOURCE_TYPE_N = 2,
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

bool operator==(const GnomeMessage& lhs, const GnomeMessage& rhs) {
	return (
		lhs.message_type == rhs.message_type &&
		lhs.resource_type == rhs.resource_type &&
		lhs.lamport_timestamp == rhs.lamport_timestamp &&
		lhs.proc_id == rhs.proc_id &&
		lhs.sent_ack == rhs.sent_ack
	);
}

struct GnomeMessageComparator {
public:
    bool operator() (const GnomeMessage& lhs, const GnomeMessage& rhs) const {
		if (lhs.lamport_timestamp != rhs.lamport_timestamp) return lhs.lamport_timestamp < rhs.lamport_timestamp;
		else return lhs.proc_id < rhs.proc_id;
    }
};

class Gnome {
	int tid = -1;
	int type = GNOME_TYPE_NONE; 
	int state = GNOME_STATE_RESTING;
	int lamport = 0; 

	std::vector<int> same_type_ids;
	std::vector<int> other_type_ids;
	std::vector<int> resources;

	// TODO: must be per resource
	std::vector<bool> ack_from_same_type;
	std::set<GnomeMessage, GnomeMessageComparator> msg_set;
	
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
			std::cout << get_debug_prefix() << "just none type, returning..." << std::endl;
		}
		
		std::cout << "Starting as requester: " << requester << std::endl;
		auto last_start = std::chrono::system_clock::now();

		while (true) {
			auto end = std::chrono::system_clock::now();
			auto elapsed = std::chrono::duration<float>(end - last_start);
			float seconds = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();


			if (state == GNOME_STATE_RESTING && requester && seconds > req_interval_s + tid) {
				request_resource((GNOME_TYPE_KILLER ? RESOURCE_TYPE_GUN : RESOURCE_TYPE_PIN_SCOPE));
				std::cerr << get_debug_prefix() << "starting REQUEST state!" << std::endl;
				last_start = std::chrono::system_clock::now();
			}

			MPI_Status status;
			int probe_flag;
			int probe_ret = MPI_Iprobe(MPI_ANY_SOURCE, GNOME_MESSAGE_TAG, MPI_COMM_WORLD, &probe_flag, &status);
			
			if (!probe_flag) continue;

			int buffer[3]; // type, timestamp, resource
			MPI_Recv(buffer, 3, MPI_INT, MPI_ANY_SOURCE, GNOME_MESSAGE_TAG, MPI_COMM_WORLD, &status);
			GnomeMessage gm = { buffer[0], buffer[1], buffer[2], status.MPI_SOURCE, false };
			react_to_message(gm);
		}
	}
private:

	std::string get_debug_prefix() {
		std::stringstream iss;
		char type_letter = '?';
		if (type == GNOME_TYPE_WORKER) type_letter = 'W';
		else if (type == GNOME_TYPE_KILLER) type_letter = 'K';
		iss << type_letter << "[" << tid << "] [t" << lamport << "]: ";
		return iss.str();
	}

	void react_to_message(GnomeMessage gm) {
		// TODO: check if needed
		int old_lamport = lamport;
		lamport = std::max(gm.lamport_timestamp, lamport) + 1;

		switch (gm.message_type) {
			case MESSAGE_TYPE_REQUEST: {
				if (type == GNOME_TYPE_KILLER) {
					resources[gm.resource_type]--;
				} 
				// Send ACK only if currently in position queue in first <resource_cnt> positions
				// - mark "ACK sent" flag

				auto gm_it = msg_set.upper_bound(gm);
				int idx = std::distance(msg_set.begin(), gm_it);
				
				// if we believe that resource is obtainable, sent ACK
				assert(resources.size() > gm.resource_type);

				if (idx <= resources[gm.resource_type]) {
					std::cerr << get_debug_prefix() << "I think " << gm.proc_id << " will be now " << idx << " in my queue, so I send ACK" << std::endl;
					send_ack(gm.proc_id, gm.resource_type);
					gm.sent_ack = true;
				}  

				// assume process sends 1 request, waits for ACK
				msg_set.insert(gm);
			} break;
			case MESSAGE_TYPE_RELEASE: {
				// Check if we can send "ACK" for elements in queue, after deleting first
				// TODO: type can be renewable? 
				if (type == GNOME_TYPE_KILLER) {
					resources[gm.resource_type]++;
				} 
				
				if (!msg_set.empty()) {
					auto first_it = msg_set.begin();
					msg_set.erase(first_it);
					std::cerr << get_debug_prefix() << "Received RELEASE from other group: erasing " << gm.proc_id << ", " << gm.lamport_timestamp << std::endl;
				} else {
					std::cerr << get_debug_prefix() << "Received RELEASE from other group: but set empty []" << std::endl;
				}

				
				// check for not sent last <resource_cnt> element 
				// - we erased first element -> check for next in "Window"
				// 
				if (msg_set.size()) {
					auto it = msg_set.begin();
					std::advance(it, resources[gm.resource_type]);
					if (it != msg_set.end() && !it->sent_ack) {
						GnomeMessage gm_copy = *(it);
						gm_copy.sent_ack = true;
						
						std::cerr << get_debug_prefix() << "RELEASE free, so Sending ACK to " << gm_copy.proc_id << ", " << gm_copy.lamport_timestamp << std::endl;
						send_ack(gm_copy.proc_id, gm.resource_type);
						
						msg_set.erase(it);
						msg_set.insert(gm_copy);
					}

				}
			} break;
			case MESSAGE_TYPE_ACK: {
				std::cout << get_debug_prefix() << "Received ACK from " << gm.proc_id << std::endl;
				
				int idx = std::distance(same_type_ids.begin(), std::find(same_type_ids.begin(), same_type_ids.end(), gm.proc_id));
				assert(ack_from_same_type[idx] == false);
				ack_from_same_type[idx] = true;
				
				if (std::all_of(ack_from_same_type.begin(), ack_from_same_type.end(), [](bool x){ return x; })) {
					std::cout << get_debug_prefix() << "ALL ACK Combined! -> Clearing and sending releases " << gm.proc_id << std::endl;
					state = GNOME_STATE_RESTING;
					std::fill(ack_from_same_type.begin(), ack_from_same_type.end(), false);
					// combined resource, send new GUN resource
					// TODO: make sure sending ALL new resources
					
					if (type == GNOME_TYPE_WORKER) {
						release_resource(RESOURCE_TYPE_GUN);
					} else if (type == GNOME_TYPE_KILLER) {
						release_resource(RESOURCE_TYPE_PIN_SCOPE);
					}
				}
				
			} break;
		}
	} 
	
	void send_ack(int gnome_id, int resource) {
		lamport++;
		int buffer[3] = { MESSAGE_TYPE_ACK, resource, lamport };
		MPI_Send(buffer, 3, MPI_INT, gnome_id, GNOME_MESSAGE_TAG, MPI_COMM_WORLD);
	}
	
	void request_resource(int resource) {
		// If I am worker/killer -> broadcast message to all workers/killers that I want to take resource
		state = GNOME_STATE_REQUESTING;
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
	
	// TODO: change resources to both Pins and Scopes
	std::vector<int> resources { N_SCOPES, 0 };
	
	Gnome gnome(tid, my_type, workers, killers, resources);
	if (tid == 0 || tid == 2) {
		gnome.perform(true);
	} else {
		gnome.perform();
	}

	MPI_Finalize(); // Musi być w każdym programie na końcu
}
