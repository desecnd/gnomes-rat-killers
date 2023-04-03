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
#include <thread>
#include <map>

// TODO: move to arguments 
#define N_WORKERS 	5
#define N_KILLERS 	1
#define N_PINS 		1 
#define N_SCOPES 	1 
#define N_WEAPONS 	0 

#define RANDOMIZE_STATE_TIMES 1
#define RANDOM_MIN_TIME_S 3
#define RANDOM_MAX_TIME_S 8

#define GNOME_ID_INVALID -1
#define GNOME_MESSAGE_TAG 123
		
enum {
	MESSAGE_TYPE_NONE = -1,
	MESSAGE_TYPE_REQUEST,
	MESSAGE_TYPE_ACK,
	MESSAGE_TYPE_CONSUME,
	MESSAGE_TYPE_PRODUCE,
	MESSAGE_TYPE_N,
};

enum { 
	RESOURCE_TYPE_NONE = -1,
	RESOURCE_TYPE_PIN_SCOPE,
	// RESOURCE_TYPE_SCOPE,
	RESOURCE_TYPE_WEAPON,
	RESOURCE_TYPE_N,
};

enum {
	GNOME_STATE_NONE = -1,
	GNOME_STATE_SLEEPING,
	GNOME_STATE_RESTING,
	GNOME_STATE_REQUESTING, 
	GNOME_STATE_INSECTION,
	GNOME_STATE_N,
};

enum {
	GNOME_TYPE_NONE = -1,
	GNOME_TYPE_WORKER,
	GNOME_TYPE_KILLER,
};


// Assumes one resource
class GnomeResourceQueue { 

	struct GnomeQueueEntry {
		int gnome_id, lamport_timestamp;
	};
	
	struct GnomeQueueEntryComparator {
    	bool operator() (GnomeQueueEntry lhs, GnomeQueueEntry rhs) const {
			if (lhs.lamport_timestamp != rhs.lamport_timestamp) return lhs.lamport_timestamp < rhs.lamport_timestamp;
			else return lhs.gnome_id < rhs.gnome_id;
		}
	};
	
	int resource_cnt = 0;
	std::map<int, bool> ack_sent;

	std::set<GnomeQueueEntry, GnomeQueueEntryComparator> req_queue;
public:
	
	std::string get_debug_info() {
		std::stringstream ss;
		ss << "GnomeResourceQueue{" << resource_cnt << " res; q=[";
		for (GnomeQueueEntry entry : req_queue) {
			ss << "(" << entry.gnome_id << "," << entry.lamport_timestamp << "), ";
		}
		ss << "]; acks: ["; 
		for (auto p : ack_sent) {
			ss << p.first << ", ";
		}
		ss << "]}";
		return ss.str();
	}
	
	void init(int n_resources) {
		resource_cnt = n_resources;
	}

	// REQUEST received -> possible sending ACK 
	// - check if entry is in ack window [0, <resource_cnt>)
	// - * if yes: mark send ack
	bool add_request(int gnome_id, int lamport) {
		GnomeQueueEntry entry = { gnome_id, lamport };

		// make sure request does not exist
		auto entry_it = req_queue.begin();
		while (entry_it != req_queue.end() && entry_it->gnome_id != gnome_id) entry_it++;
		assert(entry_it == req_queue.end());

		// returns iterator to inserted element, and true if succeeded
		auto p = req_queue.insert(entry); 
		int idx = std::distance(req_queue.begin(), p.first);

		// new request in ACK window
		return idx < resource_cnt;
	}

	// CONSUME resource -> remove request, no option in sending ACK
	// - remove entry from Queue
	// - set ack_sent to false for this gnome
	// - decrement resource
	void consume_resource(int gnome_id) {
		auto entry_it = req_queue.begin();
		while (entry_it != req_queue.end() && entry_it->gnome_id != gnome_id) entry_it++;

		// make sure gnome was in the queue
		assert(entry_it != req_queue.end());

		// make sure gnome was in the window?
		int idx = std::distance(req_queue.begin(), entry_it);
		assert(idx < resource_cnt);

		// make sure we sent ack to gnome:
		assert(ack_sent.at(gnome_id) == true);
		
		// action:
		ack_sent[gnome_id] = false;
		req_queue.erase(entry_it);
		resource_cnt--;
	}
	
	// PRODUCE resource -> possible 1 ack to send (enlarging the ack window)
	// - increment cnt
	// - check if <resource_cnt>-th index needs ack
	int produce_resource() {
		resource_cnt++;
		if (resource_cnt - 1 < req_queue.size()) {
			auto entry_it = req_queue.begin();
			std::advance(entry_it, resource_cnt - 1);
			assert(entry_it != req_queue.end());

			int gnome_id = entry_it->gnome_id;
			
			// not found
			if ((ack_sent.find(gnome_id) == ack_sent.end()) || (ack_sent.at(gnome_id) == false)) {
				return gnome_id;
			}
		} 

		return GNOME_ID_INVALID;
	}
	
	void mark_ack_sent(int gnome_id) {
		if (ack_sent.find(gnome_id) != ack_sent.end()) {
			assert(ack_sent.at(gnome_id) == false);
		}
		ack_sent[gnome_id] = true;
	}
};

struct GnomeMessage {
	int message_type;
	int resource_type; 
	int lamport_timestamp;
};

class Gnome {
	int tid = -1;
	int type = GNOME_TYPE_NONE; 
	int state = GNOME_STATE_NONE;
	int lamport = 0; 
	int req_resource = RESOURCE_TYPE_NONE;

	std::vector<int> same_type_ids;
	std::vector<int> other_type_ids;

	std::map<int, bool> received_ack;
	int received_ack_cnt = 0;
	bool ack_myself = false;

	std::map<int, GnomeResourceQueue> resource_queues;
	std::array<int, GNOME_STATE_N> state_time; 

	
public:
	Gnome(int g_tid, int g_type, const std::vector<int>& workers, const std::vector<int>& killers) {
		tid = g_tid;
		type = g_type;
		lamport = 0;
		state = GNOME_STATE_SLEEPING;
		req_resource = RESOURCE_TYPE_NONE;
		state_time = { 3, 5, 2, 3 };
		ack_myself = false;
		
		if (type == GNOME_TYPE_WORKER) {
			same_type_ids = workers;
			other_type_ids = killers;
		} else if (type == GNOME_TYPE_KILLER) {
			same_type_ids = killers;
			other_type_ids = workers;
		}
		
		clear_received_ack();
	}
	
	void init_resources(const std::vector<std::pair<int, int>>& resources) {
		for (auto p : resources) {
			int r = p.first; 
			int rc = p.second; 
			resource_queues[r] = GnomeResourceQueue();
			resource_queues[r].init(rc);
		}
	}
	
	// can consume only one resource
	bool all_gnomes_agreed() {
		// all gnomes approved
		return (received_ack_cnt == same_type_ids.size()) && ack_myself;
	}
	
	void clear_received_ack() {
		for (int id : same_type_ids) {
			received_ack[id] = false;
		}
		received_ack_cnt = 0;
		ack_myself = false;
	}

	void act_as_worker() {
		static auto last_transition = std::chrono::system_clock::now();
		
		auto current_time = std::chrono::system_clock::now();
		auto elapsed = std::chrono::duration<float>(current_time - last_transition);
		float seconds = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();

		// transition SLEEPING -> RESTING
		if (state == GNOME_STATE_SLEEPING) {
			// TODO: change
			std::cerr << get_debug_prefix() << "Falling asleep... (SLEEP) {" << state_time[state] << "s}" << std::endl;
			std::this_thread::sleep_for(std::chrono::seconds(state_time[state]));
			state = GNOME_STATE_RESTING;
			std::cerr << get_debug_prefix() << "Now, will rest a bit... (SLEEP -> REST) {" << state_time[state] << "s}" << std::endl;
		} else if (seconds < state_time[state]) {
			// not ready yet
			return;
		}
		// transition RESTING -> REQUESTING
		else if (state == GNOME_STATE_RESTING) {
			std::cerr << get_debug_prefix() << "Acquiring pin & scope! (REST -> REQ)" << std::endl;
			req_resource = RESOURCE_TYPE_PIN_SCOPE;

			// Add our request, check if we have ACK window, if not, do not mark it
			// Maybe we run out of resources, or we are waiting for PRODUCE
			bool req_in_window = resource_queues[req_resource].add_request(tid, lamport);
			if (req_in_window) {
				ack_myself = true;
				resource_queues[req_resource].mark_ack_sent(tid);
			}

			send_request_resource(req_resource);
			state = GNOME_STATE_REQUESTING;
		}
		
		// transition REQUESTING -> INSECTION
		else if (state == GNOME_STATE_REQUESTING && all_gnomes_agreed()) {
			auto& q = resource_queues[req_resource];
			// std::cerr << get_debug_prefix() << "Assembling the next weapon of mass ratstruction! (using pins for next " << state_time[GNOME_STATE_INSECTION] << " seconds) because all gnomes: " << all_gnomes_agreed() << " - " << q.get_debug_info() << std::endl;

			state = GNOME_STATE_INSECTION;
			std::cerr << get_debug_prefix() << "Assembing the weapon of mass ratstruction! (REQ -> WORK) {" << state_time[state] << "s}" << std::endl;
		}
		
		// transition INSECTION -> SLEEPING 
		else if (state == GNOME_STATE_INSECTION) {
			std::cerr << get_debug_prefix() << "Delivering the weapon... (WORK -> SLEEP)" << std::endl;
			
			clear_received_ack();

			resource_queues[req_resource].consume_resource(tid);
			send_consume_resource(req_resource);

			req_resource = RESOURCE_TYPE_NONE;

			// TODO: make sure to send both after transition
			send_produce_resource(RESOURCE_TYPE_WEAPON);
			state = GNOME_STATE_SLEEPING;
		}

		last_transition = std::chrono::system_clock::now();
	}
	
	// Set state times to random values
	void roll_state_times(int min_time_s = RANDOM_MIN_TIME_S, int max_time_s = RANDOM_MAX_TIME_S) {
		for (int &time_s : state_time) {
			time_s = rand() % (max_time_s - min_time_s) + min_time_s;
		}
	}

	void act_as_killer() {
		static auto last_transition = std::chrono::system_clock::now();
		
		auto current_time = std::chrono::system_clock::now();
		auto elapsed = std::chrono::duration<float>(current_time - last_transition);
		float seconds = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();

		// transition SLEEPING -> RESTING
		if (state == GNOME_STATE_SLEEPING) {
			

			std::cerr << get_debug_prefix() << "Falling asleep... (SLEEP) {" << state_time[state] << "s}" << std::endl;
			std::this_thread::sleep_for(std::chrono::seconds(state_time[state]));
			state = GNOME_STATE_RESTING;
			std::cerr << get_debug_prefix() << "Will rest for a bit... (SLEEP -> REST) {" << state_time[state] << "s}" << std::endl;
		} else if (seconds < state_time[state]) {
			// not ready yet
			return;
		}
		// transition RESTING -> REQUESTING
		else if (state == GNOME_STATE_RESTING) {
			std::cerr << get_debug_prefix() << "I need FiRePoWeR! (REST -> REQ)" << std::endl;
			req_resource = RESOURCE_TYPE_WEAPON;

			// Add our request, check if we have ACK window, if not, do not mark it
			// Maybe we run out of resources, or we are waiting for PRODUCE
			bool req_in_window = resource_queues[req_resource].add_request(tid, lamport);
			if (req_in_window) {
				ack_myself = true;
				resource_queues[req_resource].mark_ack_sent(tid);
			}

			send_request_resource(req_resource);
			state = GNOME_STATE_REQUESTING;
		}
		
		// transition REQUESTING -> INSECTION
		else if (state == GNOME_STATE_REQUESTING && all_gnomes_agreed()) {
			state = GNOME_STATE_INSECTION;
			std::cerr << get_debug_prefix() << "Sending the next RAT to the moon, boyz! (REQ -> WORK) {" << state_time[state] <<"s}" << std::endl;
		}
		
		// transition INSECTION -> SLEEPING 
		else if (state == GNOME_STATE_INSECTION) {
			std::cerr << get_debug_prefix() << "Headhunterz are back... (WORK -> SLEEP)" << std::endl;
			
			// TODO: make sure to send both after transition
			clear_received_ack();
			resource_queues[req_resource].consume_resource(tid);
			send_consume_resource(req_resource);
			req_resource = RESOURCE_TYPE_NONE;

			send_produce_resource(RESOURCE_TYPE_PIN_SCOPE);
			state = GNOME_STATE_SLEEPING;
		}

		last_transition = std::chrono::system_clock::now();
	}

	void perform() {
		if (type == GNOME_TYPE_NONE) {
			std::cout << get_debug_prefix() << "I am type NONE, returning..." << std::endl;
			return;
		}
		
		while (true) {

			if (RANDOMIZE_STATE_TIMES) roll_state_times();

			if (type == GNOME_TYPE_WORKER) act_as_worker();
			else if (type == GNOME_TYPE_KILLER) act_as_killer();


			// probe messages in mpi -> non-blocking
			MPI_Status status;
			int probe_flag;
			int probe_ret = MPI_Iprobe(MPI_ANY_SOURCE, GNOME_MESSAGE_TAG, MPI_COMM_WORLD, &probe_flag, &status);
			
			if (!probe_flag) continue;


			// receive probe message in mpi -> blocking, but we know there is a message
			int buffer[3]; // type, timestamp, resource
			MPI_Recv(buffer, 3, MPI_INT, MPI_ANY_SOURCE, GNOME_MESSAGE_TAG, MPI_COMM_WORLD, &status);
			GnomeMessage gm = { buffer[0], buffer[1], buffer[2] };
			react_to_message(gm, status.MPI_SOURCE);
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
	
	void react_to_message(GnomeMessage gm, int src_gnome_id) {
		lamport = std::max(gm.lamport_timestamp, lamport) + 1;
		
		if (src_gnome_id == tid) std::cerr << get_debug_prefix() << "received from myself! " << gm.message_type << std::endl;
		
		auto& queue = resource_queues[gm.resource_type];
		int new_gnome_id_in_window = GNOME_ID_INVALID;
		
		switch (gm.message_type) {
			case MESSAGE_TYPE_PRODUCE: {
				// We received PRODUCE, there is a chance, we have to send ack to new gnome
				new_gnome_id_in_window = queue.produce_resource();
				// std::cerr << get_debug_prefix() << "...received PRODUCE from " << src_gnome_id << "(something new: " << new_gnome_id_in_window << ")" << std::endl;
			} break;
			case MESSAGE_TYPE_CONSUME: {
				// std::cerr << get_debug_prefix() << "...received CONSUME from " << src_gnome_id << ", -> " << queue.get_debug_info() << std::endl;
				queue.consume_resource(src_gnome_id);
			} break;
			case MESSAGE_TYPE_REQUEST: {
				// std::cerr << get_debug_prefix() << "*** prestate: " << queue.get_debug_info() << std::endl;
				bool should_send_ack = queue.add_request(src_gnome_id, gm.lamport_timestamp);
				if (should_send_ack) new_gnome_id_in_window = src_gnome_id;
				// std::cerr << get_debug_prefix() << "...received REQUEST from " << src_gnome_id << "(should send ACK? " << should_send_ack << ", " << new_gnome_id_in_window << ")" << std::endl;
				// std::cerr << get_debug_prefix() << "*** poststate: " << queue.get_debug_info() << std::endl;
			} break;
			case MESSAGE_TYPE_ACK: {
				assert(received_ack[src_gnome_id] == false);
				received_ack[src_gnome_id] = true;
				received_ack_cnt++;
			} break;
		}

		if (new_gnome_id_in_window != GNOME_ID_INVALID) {
			// Does not matter if we are the gnome, or someone else
			// Queue keeps all ids from the same type 
			queue.mark_ack_sent(new_gnome_id_in_window);

			// We are the new gnome in window, just confirm it
			if (tid == new_gnome_id_in_window) {
				assert(ack_myself == false);
				ack_myself = true;
			// Otherwise, we send the ack
			} else {
				// std::cerr << get_debug_prefix() << "... sending ACK to " << new_gnome_id_in_window << std::endl;
				send_ack_resource(new_gnome_id_in_window, gm.resource_type);
			}
		}
	}
	
	
	void send_ack_resource(int gnome_id, int resource) {
		assert(find(same_type_ids.begin(), same_type_ids.end(), gnome_id) != same_type_ids.end());
		int buffer[3] = { MESSAGE_TYPE_ACK, resource, lamport };
		MPI_Send(buffer, 3, MPI_INT, gnome_id, GNOME_MESSAGE_TAG, MPI_COMM_WORLD);
		lamport++;
	}
	
	void send_request_resource(int resource) {
		// If I am worker/killer -> broadcast message to all workers/killers that I want to take resource
		int buffer[3] = { MESSAGE_TYPE_REQUEST, resource, lamport };
		for (auto gnome_id : same_type_ids) {
			MPI_Send(buffer, 3, MPI_INT, gnome_id, GNOME_MESSAGE_TAG, MPI_COMM_WORLD);
		}
		// incrementing the clock AFTER my request, otherwise, we push into queue wrong value
		lamport++;
	}

	void send_consume_resource(int resource) {
		// If I am worker/killer -> broadcast message to all killers/workers that I want released resource
		int buffer[3] = { MESSAGE_TYPE_CONSUME, resource, lamport };
		for (auto gnome_id : same_type_ids) {
			MPI_Send(buffer, 3, MPI_INT, gnome_id, GNOME_MESSAGE_TAG, MPI_COMM_WORLD);
		}
		lamport++;
	}
	
	void send_produce_resource(int resource) {
		int buffer[3] = { MESSAGE_TYPE_PRODUCE, resource, lamport };
		for (auto gnome_id : other_type_ids) {
			MPI_Send(buffer, 3, MPI_INT, gnome_id, GNOME_MESSAGE_TAG, MPI_COMM_WORLD);
		}
		lamport++;
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

	srand(time(0) + tid);
	// printf("My id is %d from %d\n",tid, size);
	
	// We can pass maximums for both workers and killers
	// - if possible we try to get all workers
	// - we try to assign what left to killers
	int n_workers = std::min(N_WORKERS, size);
	int n_killers = std::min(size - n_workers, N_KILLERS);
	
	std::vector<int> workers;
	std::vector<int> killers;
	int my_type = assign_gnome_roles(workers, killers, n_workers, n_killers, tid);
	
	// TODO: change resources to both Pins and Scopes
	
	Gnome gnome(tid, my_type, workers, killers) ;
	std::vector<std::pair<int, int>> resources;

	if (my_type == GNOME_TYPE_KILLER) {
		resources.push_back({RESOURCE_TYPE_WEAPON, N_WEAPONS});
	} else if (my_type == GNOME_TYPE_WORKER) {
		resources.push_back({RESOURCE_TYPE_PIN_SCOPE, std::min(N_PINS, N_SCOPES)});
		// TODO: Add second resource
	}
	
	gnome.init_resources(resources);
	gnome.perform();

	MPI_Finalize(); 
}
