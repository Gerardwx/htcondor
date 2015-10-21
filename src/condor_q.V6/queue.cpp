/***************************************************************
 *
 * Copyright (C) 1990-2007, Condor Team, Computer Sciences Department,
 * University of Wisconsin-Madison, WI.
 * 
 * Licensed under the Apache License, Version 2.0 (the "License"); you
 * may not use this file except in compliance with the License.  You may
 * obtain a copy of the License at
 * 
 *    http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 ***************************************************************/


#include "condor_common.h"
#include "condor_config.h"
#include "condor_accountant.h"
#include "condor_classad.h"
#include "condor_debug.h"
#include "condor_query.h"
#include "condor_q.h"
#include "condor_io.h"
#include "condor_string.h"
#include "condor_attributes.h"
#include "match_prefix.h"
#include "get_daemon_name.h"
#include "MyString.h"
#include "ad_printmask.h"
#include "internet.h"
#include "sig_install.h"
#include "format_time.h"
#include "daemon.h"
#include "dc_collector.h"
#include "basename.h"
#include "metric_units.h"
#include "globus_utils.h"
#include "error_utils.h"
#include "print_wrapped_text.h"
#include "condor_distribution.h"
#include "string_list.h"
#include "condor_version.h"
#include "subsystem_info.h"
#include "condor_open.h"
#include "condor_sockaddr.h"
#include "condor_id.h"
#include "userlog_to_classads.h"
#include "ipv6_hostname.h"
#include "../condor_procapi/procapi.h" // for getting cpu time & process memory
#include <map>
#include <vector>
#include "../classad_analysis/analysis.h"
//#include "pool_allocator.h"
#include "expr_analyze.h"
#include "classad/classadCache.h" // for CachedExprEnvelope stats

static int cleanup_globals(int exit_code); // called on exit to do necessary cleanup
#define exit(n) (exit)(cleanup_globals(n))

#ifdef HAVE_EXT_POSTGRESQL
#include "sqlquery.h"
#endif /* HAVE_EXT_POSTGRESQL */

/* Since this enum can have conditional compilation applied to it, I'm
	specifying the values for it to keep their actual integral values
	constant in case someone decides to write this information to disk to
	pass it to another daemon or something. This enum is used to determine
	who to talk to when using the -direct option. */
enum {
	/* don't know who I should be talking to */
	DIRECT_UNKNOWN = 0,
	/* start at the rdbms and fail over like normal */
	DIRECT_ALL = 1,
#ifdef HAVE_EXT_POSTGRESQL
	/* talk directly to the rdbms system */
	DIRECT_RDBMS = 2,
	/* talk directly to the quill daemon */
	DIRECT_QUILLD = 3,
#endif
	/* talk directly to the schedd */
	DIRECT_SCHEDD = 4
};

struct 	PrioEntry { MyString name; float prio; };

#ifdef WIN32
static int getConsoleWindowSize(int * pHeight = NULL) {
	CONSOLE_SCREEN_BUFFER_INFO ws;
	if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &ws)) {
		if (pHeight)
			*pHeight = (int)(ws.srWindow.Bottom - ws.srWindow.Top)+1;
		return (int)ws.dwSize.X;
	}
	return 80;
}
#else
#include <sys/ioctl.h> 
static int getConsoleWindowSize(int * pHeight = NULL) {
    struct winsize ws; 
	if (0 == ioctl(0, TIOCGWINSZ, &ws)) {
		//printf ("lines %d\n", ws.ws_row); 
		//printf ("columns %d\n", ws.ws_col); 
		if (pHeight)
			*pHeight = (int)ws.ws_row;
		return (int) ws.ws_col;
	}
	return 80;
}
#endif

static  int  testing_width = 0;
static int getDisplayWidth() {
	if (testing_width <= 0) {
		testing_width = getConsoleWindowSize();
		if (testing_width <= 0)
			testing_width = 80;
	}
	return testing_width;
}

extern 	"C" int SetSyscalls(int val){return val;}
extern  void short_print(int,int,const char*,int,int,int,int,int,const char *);
static  void processCommandLineArguments(int, char *[]);

//static  bool process_buffer_line_old(void*, ClassAd *);
static  bool process_job_and_render_to_dag_map(void*, ClassAd*);
static  bool process_and_print_job(void*, ClassAd*);
typedef bool (* buffer_line_processor)(void*, ClassAd *);

static 	void short_header (void);
static 	void usage (const char *, int other=0);
enum { usage_Universe=1, usage_JobStatus=2, usage_AllOther=0xFF };
static 	void buffer_io_display (std::string & out, ClassAd *);
static 	char * bufferJobShort (ClassAd *);
static const char * format_job_status_char(int job_status, AttrList*ad, Formatter &);

// functions to fetch job ads and print them out
//
static bool show_file_queue(const char* jobads, const char* userlog);
static bool show_schedd_queue(const char* scheddAddress, const char* scheddName, const char* scheddMachine, int useFastPath);
#ifdef HAVE_EXT_POSTGRESQL
static bool show_db_queue( const char* quill_name, const char* db_ipAddr, const char* db_name, const char* query_password);
#endif

static void init_output_mask();

static bool read_classad_file(const char *filename, ClassAdList &classads, ClassAdFileParseHelper* pparse_help, const char * constr);
static int read_userprio_file(const char *filename, ExtArray<PrioEntry> & prios);



/* convert the -direct aqrgument prameter into an enum */
unsigned int process_direct_argument(char *arg);

#ifdef HAVE_EXT_POSTGRESQL
static void quillCleanupOnExit(int n);
static void freeConnectionStrings();

// use std::strings to hold these strings so that we don't have to worry about freeing them on exit.
static std::string squillName, squeryPassword, sdbIpAddr, sdbName, squillMachine, quillAddr;
const char *quillName = NULL;
const char *dbIpAddr = NULL;
const char *dbName = NULL;
const char *queryPassword = NULL;
const char *quillMachine = NULL;

/* execute a database query directly */ 
static void exec_db_query(const char *quill_name, const char *db_ipAddr, const char *db_name,const char *query_password);

/* build database connection string */
static char * getDBConnStr(char const *&, char const *&, char const *&, char const *&);

/* get the quill address for the quill_name specified */
static QueryResult getQuillAddrFromCollector(const char *quill_name, std::string &quill_addr);

/* avgqueuetime is used to indicate a request to query average wait time for uncompleted jobs in queue */
static  bool avgqueuetime = false;

/* directDBquery means we will just run a database query and return results directly to user */
static  bool directDBquery = false;
#endif

/* Warn about schedd-wide limits that may confuse analysis code */
void warnScheddLimits(Daemon *schedd,ClassAd *job,MyString &result_buf);

/* who is it that I should directly ask for the queue, defaults to the normal
	failover semantics */
static unsigned int direct = DIRECT_ALL;

static 	int dash_long = 0, summarize = 1, global = 0, show_io = 0, dash_dag = 0, show_held = 0;
static  int use_xml = 0;
static  int dash_autocluster = 0;
static  bool widescreen = false;
//static  int  use_old_code = true;
static  bool expert = false;
static  bool verbose = false; // note: this is !!NOT the same as -long !!!
static  int g_match_limit = -1;

static 	int malformed, running, idle, held, suspended, completed, removed;

static  const char *jobads_file = NULL; // NULL, or points to jobads filename from argv
static  const char *machineads_file = NULL; // NULL, or points to machineads filename from argv
static  const char *userprios_file = NULL; // NULL, or points to userprios filename from argv
static  const char *userlog_file = NULL; // NULL, or points to userlog filename from argv
static  bool analyze_with_userprio = false;
static  const char * analyze_memory_usage = NULL;
static  bool dash_profile = false;
//static  bool analyze_dslots = false;
static  bool disable_user_print_files = false;


// Constraint on JobIDs for faster filtering
// a list and not set since we expect a very shallow depth
static std::vector<CondorID> constrID;

typedef std::map<int, std::vector<ClassAd *> > JobClusterMap;

static	CondorQ 	Q;
static	QueryResult result;

static	CondorQuery	scheddQuery(SCHEDD_AD);
static	CondorQuery submittorQuery(SUBMITTOR_AD);

static	ClassAdList	scheddList;

static  ClassAdAnalyzer analyzer;

static const char* format_owner(const char*, AttrList*);
static const char* format_owner_wide(const char*, AttrList*, Formatter &);

#define Q_SORT_BY_NAME

#define ALLOW_DASH_DAG
#ifdef ALLOW_DASH_DAG

// clusterProcString is the container where the output strings are
//    stored.  We need the cluster and proc so that we can sort in an
//    orderly manner (and don't need to rely on the cluster.proc to be
//    available....)

class clusterProcString {
public:
	clusterProcString();
	int dagman_cluster_id;
	int dagman_proc_id;
	int cluster;
	int proc;
	char * string;
	clusterProcString *parent;
	std::vector<clusterProcString*> children;
};

class clusterProcMapper {
public:
	int dagman_cluster_id;
	int dagman_proc_id;
	int cluster;
	int proc;
	clusterProcMapper(const clusterProcString& cps) : dagman_cluster_id(cps.dagman_cluster_id),
		dagman_proc_id(cps.dagman_proc_id), cluster(cps.cluster), proc(cps.proc) {}
	clusterProcMapper(int dci) : dagman_cluster_id(dci), dagman_proc_id(0), cluster(dci),
		proc(0) {}
};

class clusterIDProcIDMapper {
public:
	int cluster;
	int proc;
	clusterIDProcIDMapper(const clusterProcString& cps) :  cluster(cps.cluster), proc(cps.proc) {}
	clusterIDProcIDMapper(int dci) : cluster(dci), proc(0) {}
};

class CompareProcMaps {
public:
	bool operator()(const clusterProcMapper& a, const clusterProcMapper& b) const {
		if (a.cluster < 0 || a.proc < 0) return true;
		if (b.cluster < 0 || b.proc < 0) return false;
		if (a.dagman_cluster_id < b.dagman_cluster_id) { return true; }
		if (a.dagman_cluster_id > b.dagman_cluster_id) { return false; }
		if (a.dagman_proc_id < b.dagman_proc_id) { return true; }
		if (a.dagman_proc_id > b.dagman_proc_id) { return false; }
		if (a.cluster < b.cluster ) { return true; }
		if (a.cluster > b.cluster ) { return false; }
		if (a.proc    < b.proc    ) { return true; }
		if (a.proc    > b.proc    ) { return false; }
		return false;
	}
};

class CompareProcIDMaps {
public:
	bool operator()(const clusterIDProcIDMapper& a, const clusterIDProcIDMapper& b) const {
		if (a.cluster < 0 || a.proc < 0) return true;
		if (b.cluster < 0 || b.proc < 0) return false;
		if (a.cluster < b.cluster ) { return true; }
		if (a.cluster > b.cluster ) { return false; }
		if (a.proc    < b.proc    ) { return true; }
		if (a.proc    > b.proc    ) { return false; }
		return false;
	}
};

/* To save typing */
typedef std::map<clusterProcMapper,clusterProcString*,CompareProcMaps> dag_map_type;
typedef std::map<clusterIDProcIDMapper,clusterProcString*,CompareProcIDMaps> dag_cluster_map_type;
dag_map_type dag_map;
dag_cluster_map_type dag_cluster_map;

clusterProcString::clusterProcString() : dagman_cluster_id(-1), dagman_proc_id(-1),
	cluster(-1), proc(-1), string(0), parent(0) {}

#endif

/* counters for job matchmaking analysis */
typedef struct {
	int fReqConstraint;   // # of machines that don't match job Requirements
	int fOffConstraint;   // # of machines that match Job requirements, but refuse the job because of their own Requirements
	int fPreemptPrioCond; // match but are serving users with a better priority in the pool
	int fRankCond;        // match but reject the job for unknown reasons
	int fPreemptReqTest;  // match but will not currently preempt their existing job
	int fOffline;         // match but are currently offline
	int available;        // are available to run your job
	int totalMachines;
	int machinesRunningJobs; // number of machines serving other users
	int machinesRunningUsersJobs; 

	void clear() { memset((void*)this, 0, sizeof(*this)); }
} anaCounters;

static int set_print_mask_from_stream(const char * streamid, bool is_filename, StringList & attrs);

static	bool		usingPrintMask = false;
static 	printmask_headerfooter_t customHeadFoot = STD_HEADFOOT;
static std::vector<GroupByKeyInfo> group_by_keys;
static  bool		cputime = false;
static	bool		current_run = false;
static 	bool		dash_globus = false;
static	bool		dash_grid = false;
static	bool		dash_run = false;
static	bool		dash_goodput = false;
static  const char		*JOB_TIME = "RUN_TIME";
static	bool		querySchedds 	= false;
static	bool		querySubmittors = false;
static	char		constraint[4096];
static  const char *user_job_constraint = NULL; // just the constraint given by the user
static  const char *user_slot_constraint = NULL; // just the machine constraint given by the user
static  const char *global_schedd_constraint = NULL; // argument from -schedd-constraint
static  bool        single_machine = false;
static	DCCollector* pool = NULL; 
static	char		*scheddAddr;	// used by format_remote_host()
static	AttrListPrintMask 	mask;
#if 0
static  List<const char> mask_head; // The list of headings for the mask entries
//static const char * mask_headings = NULL;
#endif
static CollectorList * Collectors = NULL;

// for run failure analysis
static  int			findSubmittor( const char * );
static	void 		setupAnalysis();
static 	int			fetchSubmittorPriosFromNegotiator(ExtArray<PrioEntry> & prios);
static	void		doJobRunAnalysis(ClassAd*, Daemon*, int details);
static const char	*doJobRunAnalysisToBuffer(ClassAd *request, anaCounters & ac, int details, bool noPrio, bool showMachines);
static	void		doSlotRunAnalysis( ClassAd*, JobClusterMap & clusters, Daemon*, int console_width);
static const char	*doSlotRunAnalysisToBuffer( ClassAd*, JobClusterMap & clusters, int console_width);
static	void		buildJobClusterMap(ClassAdList & jobs, const char * attr, JobClusterMap & clusters);
static	int			better_analyze = false;
static	bool		reverse_analyze = false;
static	bool		summarize_anal = false;
static  bool		summarize_with_owner = true;
static  int			cOwnersOnCmdline = 0;
static	const char	*fixSubmittorName( const char*, int );
static	ClassAdList startdAds;
static	ExprTree	*stdRankCondition;
static	ExprTree	*preemptRankCondition;
static	ExprTree	*preemptPrioCondition;
static	ExprTree	*preemptionReq;
static  ExtArray<PrioEntry> prioTable;

static	int			analyze_detail_level = 0; // one or more of detail_xxx enum values above.

const int SHORT_BUFFER_SIZE = 8192;
const int LONG_BUFFER_SIZE = 16384;	
char return_buff[LONG_BUFFER_SIZE * 100];

StringList attrs(NULL, "\n");; // The list of attrs we want, "" for all

bool g_stream_results = false;


static int longest_slot_machine_name = 0;
static int longest_slot_name = 0;

// Sort Machine ads by Machine name first, then by slotid, then then by slot name
int SlotSort(ClassAd *ad1, ClassAd *ad2, void *  /*data*/)
{
	std::string name1, name2;
	if ( ! ad1->LookupString(ATTR_MACHINE, name1))
		return 1;
	if ( ! ad2->LookupString(ATTR_MACHINE, name2))
		return 0;
	
	// opportunisticly capture the longest slot machine name.
	longest_slot_machine_name = MAX(longest_slot_machine_name, (int)name1.size());
	longest_slot_machine_name = MAX(longest_slot_machine_name, (int)name2.size());

	int cmp = name1.compare(name2);
	if (cmp < 0) return 1;
	if (cmp > 0) return 0;

	int slot1=0, slot2=0;
	ad1->LookupInteger(ATTR_SLOT_ID, slot1);
	ad2->LookupInteger(ATTR_SLOT_ID, slot2);
	if (slot1 < slot2) return 1;
	if (slot1 > slot2) return 0;

	// PRAGMA_REMIND("TJ: revisit this code to compare dynamic slot id once that exists");
	// for now, the only way to get the dynamic slot id is to use the slot name.
	name1.clear(); name2.clear();
	if ( ! ad1->LookupString(ATTR_NAME, name1))
		return 1;
	if ( ! ad2->LookupString(ATTR_NAME, name2))
		return 0;
	// opportunisticly capture the longest slot name.
	longest_slot_name = MAX(longest_slot_name, (int)name1.size());
	longest_slot_name = MAX(longest_slot_name, (int)name2.size());
	cmp = name1.compare(name2);
	if (cmp < 0) return 1;
	return 0;
}

class CondorQClassAdFileParseHelper : public compat_classad::ClassAdFileParseHelper
{
 public:
	CondorQClassAdFileParseHelper() : is_schedd(false), is_submitter(false) {}
	virtual int PreParse(std::string & line, ClassAd & ad, FILE* file);
	virtual int OnParseError(std::string & line, ClassAd & ad, FILE* file);
	std::string schedd_name;
	std::string schedd_addr;
	bool is_schedd;
	bool is_submitter;
};

// this method is called before each line is parsed. 
// return 0 to skip (is_comment), 1 to parse line, 2 for end-of-classad, -1 for abort
int CondorQClassAdFileParseHelper::PreParse(std::string & line, ClassAd & /*ad*/, FILE* /*file*/)
{
	// treat blank lines as delimiters.
	if (line.size() <= 0) {
		return 2; // end of classad.
	}

	// standard delimitors are ... and ***
	if (starts_with(line,"\n") || starts_with(line,"...") || starts_with(line,"***")) {
		return 2; // end of classad.
	}

	// the normal output of condor_q -long is "-- schedd-name <addr>"
	// we want to treat that as a delimiter, and also capture the schedd name and addr
	if (starts_with(line, "-- ")) {
		is_schedd = starts_with(line.substr(3), "Schedd:");
		is_submitter = starts_with(line.substr(3), "Submitter:");
		if (is_schedd || is_submitter) {
			size_t ix1 = schedd_name.find(':');
			schedd_name = line.substr(ix1+1);
			ix1 = schedd_name.find_first_of(": \t\n");
			if (ix1 != string::npos) {
				size_t ix2 = schedd_name.find_first_not_of(": \t\n", ix1);
				if (ix2 != string::npos) {
					schedd_addr = schedd_name.substr(ix2);
					ix2 = schedd_addr.find_first_of(" \t\n");
					if (ix2 != string::npos) {
						schedd_addr = schedd_addr.substr(0,ix2);
					}
				}
				schedd_name = schedd_name.substr(0,ix1);
			}
		}
		return 2;
	}


	// check for blank lines or lines whose first character is #
	// tell the parser to skip those lines, otherwise tell the parser to
	// parse the line.
	for (size_t ix = 0; ix < line.size(); ++ix) {
		if (line[ix] == '#' || line[ix] == '\n')
			return 0; // skip this line, but don't stop parsing.
		if (line[ix] != ' ' && line[ix] != '\t')
			break;
	}
	return 1; // parse this line
}

// this method is called when the parser encounters an error
// return 0 to skip and continue, 1 to re-parse line, 2 to quit parsing with success, -1 to abort parsing.
int CondorQClassAdFileParseHelper::OnParseError(std::string & line, ClassAd & ad, FILE* file)
{
	// when we get a parse error, skip ahead to the start of the next classad.
	int ee = this->PreParse(line, ad, file);
	while (1 == ee) {
		if ( ! readLine(line, file, false) || feof(file)) {
			ee = 2;
			break;
		}
		ee = this->PreParse(line, ad, file);
	}
	return ee;
}

static void
profile_print(size_t & cbBefore, double & tmBefore, int cAds, bool fCacheStats=true)
{
       double tmAfter = 0.0;
       size_t cbAfter = ProcAPI::getBasicUsage(getpid(), &tmAfter);
       fprintf(stderr, " %d ads (caching %s)", cAds, classad::ClassAdGetExpressionCaching() ? "ON" : "OFF");
       fprintf(stderr, ", %.3f CPU-sec, %" PRId64 " bytes (from %" PRId64 " to %" PRId64 ")\n",
               (tmAfter - tmBefore), (PRId64_t)(cbAfter - cbBefore), (PRId64_t)cbBefore, (PRId64_t)cbAfter);
       tmBefore = tmAfter; cbBefore = cbAfter;
       if (fCacheStats) {
               classad::CachedExprEnvelope::_debug_print_stats(stderr);
       }
}

#ifdef HAVE_EXT_POSTGRESQL
/* this function for checking whether database can be used for querying in local machine */
static bool checkDBconfig() {
	char *tmp;

	if (param_boolean("QUILL_ENABLED", false) == false) {
		return false;
	};

	tmp = param("QUILL_NAME");
	if (!tmp) {
		return false;
	}
	free(tmp);

	tmp = param("QUILL_DB_IP_ADDR");
	if (!tmp) {
		return false;
	}
	free(tmp);

	tmp = param("QUILL_DB_NAME");
	if (!tmp) {
		return false;
	}
	free(tmp);

	tmp = param("QUILL_DB_QUERY_PASSWORD");
	if (!tmp) {
		return false;
	}
	free(tmp);

	return true;
}
#endif /* HAVE_EXT_POSTGRESQL */

int main (int argc, char **argv)
{
	ClassAd		*ad;
	bool		first;
	char		*scheddName=NULL;
	char		scheddMachine[64];
	int		useFastScheddQuery = 0;
	char		*tmp;
	bool        useDB; /* Is there a database to query for a schedd */
	int         retval = 0;

	Collectors = NULL;

	// load up configuration file
	myDistro->Init( argc, argv );
	config();
	dprintf_config_tool_on_error(0);
	dprintf_OnExitDumpOnErrorBuffer(stderr);
	//classad::ClassAdSetExpressionCaching( param_boolean( "ENABLE_CLASSAD_CACHING", false ) );

#ifdef HAVE_EXT_POSTGRESQL
		/* by default check the configuration for local database */
	useDB = checkDBconfig();
#else 
	useDB = FALSE;
	if (useDB) {} /* Done to suppress set-but-not-used warnings */
#endif /* HAVE_EXT_POSTGRESQL */

#if !defined(WIN32)
	install_sig_handler(SIGPIPE, SIG_IGN );
#endif

	// process arguments
	processCommandLineArguments (argc, argv);

	// Since we are assuming that we want to talk to a DB in the normal
	// case, this will ensure that we don't try to when loading the job queue
	// classads from file.
	if ((jobads_file != NULL) || (userlog_file != NULL)) {
		useDB = FALSE;
	}

	if (Collectors == NULL) {
		Collectors = CollectorList::create();
	}

	// check if analysis is required
	if( better_analyze ) {
		setupAnalysis();
	}

	// if fetching jobads from a file or userlog, we don't need to init any daemon or database communication
	if ((jobads_file != NULL || userlog_file != NULL)) {
		retval = show_file_queue(jobads_file, userlog_file);
		exit(retval?EXIT_SUCCESS:EXIT_FAILURE);
	}

	// if we haven't figured out what to do yet, just display the
	// local queue 
	if (!global && !querySchedds && !querySubmittors) {

		Daemon schedd( DT_SCHEDD, 0, 0 );

		if ( schedd.locate() ) {

			scheddAddr = strdup(schedd.addr());
			if( (tmp = schedd.name()) ) {
				scheddName = strdup(tmp);
				Q.addSchedd(scheddName);
			} else {
				scheddName = strdup("Unknown");
			}
			if( (tmp = schedd.fullHostname()) ) {
				sprintf( scheddMachine, "%s", tmp );
			} else {
				sprintf( scheddMachine, "Unknown" );
			}
			if (schedd.version()) {
				CondorVersionInfo v(schedd.version());
				useFastScheddQuery = v.built_since_version(6,9,3) ? 1 : 0;
				if (v.built_since_version(8, 3, 3)) {
					useFastScheddQuery = 2;
				}
			}
			
#ifdef HAVE_EXT_POSTGRESQL
			if ( directDBquery ) {
				/* perform direct DB query if indicated and exit */

					/* check if database is available */
				if (!useDB) {
					printf ("\n\n-- Schedd: %s : %s\n", scheddName, scheddAddr);
					fprintf(stderr, "Database query not supported on schedd: %s\n", scheddName);
				}

				exec_db_query(NULL, NULL, NULL, NULL);
				exit(EXIT_SUCCESS);
			} 
#endif /* HAVE_EXT_POSTGRESQL */

			/* .. not a direct db query, so just happily continue ... */

			init_output_mask();
			
			/* When an installation has database parameters configured, 
				it means there is quill daemon. If database
				is not accessible, we fail over to
				quill daemon, and if quill daemon
				is not available, we fail over the
				schedd daemon */
			switch(direct)
			{
				case DIRECT_ALL:
					/* always try the failover sequence */

					/* FALL THROUGH */

#ifdef HAVE_EXT_POSTGRESQL
				case DIRECT_RDBMS:
					if (useDB) {

						/* ask the database for the queue */
						retval = show_db_queue(NULL, NULL, NULL, NULL);
						if (retval)
						{
							/* if the queue was retrieved, then I am done */
							exit(retval?EXIT_SUCCESS:EXIT_FAILURE);
						}
						
						fprintf( stderr, 
							"-- Database not reachable or down.\n");

						if (direct == DIRECT_RDBMS) {
							fprintf(stderr,
								"\t- Not failing over due to -direct\n");
							exit(retval?EXIT_SUCCESS:EXIT_FAILURE);
						} 

						fprintf(stderr,
							"\t- Failing over to the quill daemon --\n");

						/* Hmm... couldn't find the database, so try the 
							quilld */
					} else {
						if (direct == DIRECT_RDBMS) {
							fprintf(stderr, 
								"-- Direct query to rdbms on behalf of\n"
								"\tschedd %s(%s) failed.\n"
								"\t- Schedd doesn't appear to be using "
								"quill.\n",
								scheddName, scheddAddr);
							fprintf(stderr,
								"\t- Not failing over due to -direct\n");
							exit(EXIT_FAILURE);
						}
					}

					/* FALL THROUGH */

				case DIRECT_QUILLD:
					if (useDB) {

						Daemon quill( DT_QUILL, 0, 0 );
						char tmp_char[8];
						strcpy(tmp_char, "Unknown");

						/* query the quill daemon */
						if (quill.locate() &&
							(retval = show_schedd_queue(quill.addr(),
								quill.name() ? quill.name() : "Unknown",
								quill.fullHostname() ? quill.fullHostname() : "Unknown",
								false))
							)
						{
							/* if the queue was retrieved, then I am done */
							exit(retval?EXIT_SUCCESS:EXIT_FAILURE);
						}

						fprintf( stderr,
							"-- Quill daemon at %s(%s)\n"
							"\tassociated with schedd %s(%s)\n"
							"\tis not reachable or can't talk to rdbms.\n",
							quill.name(), quill.addr(),
							scheddName, scheddAddr );

						if (direct == DIRECT_QUILLD) {
							fprintf(stderr,
								"\t- Not failing over due to -direct\n");
							exit(retval?EXIT_SUCCESS:EXIT_FAILURE);
						}

						fprintf(stderr,
							"\t- Failing over to the schedd %s(%s).\n", 
							scheddName, scheddAddr);

					} else {
						if (direct == DIRECT_QUILLD) {
							fprintf(stderr,
								"-- Direct query to quilld associated with\n"
								"\tschedd %s(%s) failed.\n"
								"\t- Schedd doesn't appear to be using "
								"quill.\n",
								scheddName, scheddAddr);

							fprintf(stderr,
								"\t- Not failing over due to use of -direct\n");

							exit(EXIT_FAILURE);
						}
					}

#endif /* HAVE_EXT_POSTGRESQL */
				case DIRECT_SCHEDD:
					retval = show_schedd_queue(scheddAddr, scheddName, scheddMachine, useFastScheddQuery);
			
					/* Hopefully I got the queue from the schedd... */
					exit(retval?EXIT_SUCCESS:EXIT_FAILURE);
					break;

				case DIRECT_UNKNOWN:
				default:
					fprintf( stderr,
						"-- Cannot determine any location for queue "
						"using option -direct!\n"
						"\t- This is an internal error and should be "
						"reported to condor-admin@cs.wisc.edu." );
					exit(EXIT_FAILURE);
					break;
			}
		} 
		
		/* I couldn't find a local schedd, so dump a message about what
			happened. */

		fprintf( stderr, "Error: %s\n", schedd.error() );
		if (!expert) {
			fprintf(stderr, "\n");
			print_wrapped_text("Extra Info: You probably saw this "
							   "error because the condor_schedd is "
							   "not running on the machine you are "
							   "trying to query.  If the condor_schedd "
							   "is not running, the Condor system "
							   "will not be able to find an address "
							   "and port to connect to and satisfy "
							   "this request.  Please make sure "
							   "the Condor daemons are running and "
							   "try again.\n", stderr );
			print_wrapped_text("Extra Info: "
							   "If the condor_schedd is running on the "
							   "machine you are trying to query and "
							   "you still see the error, the most "
							   "likely cause is that you have setup a " 
							   "personal Condor, you have not "
							   "defined SCHEDD_NAME in your "
							   "condor_config file, and something "
							   "is wrong with your "
							   "SCHEDD_ADDRESS_FILE setting. "
							   "You must define either or both of "
							   "those settings in your config "
							   "file, or you must use the -name "
							   "option to condor_q. Please see "
							   "the Condor manual for details on "
							   "SCHEDD_NAME and "
							   "SCHEDD_ADDRESS_FILE.",  stderr );
		}

		exit( EXIT_FAILURE );
	}
	
	// if a global queue is required, query the schedds instead of submittors
	if (global) {
		querySchedds = true;
		sprintf( constraint, "%s > 0 || %s > 0 || %s > 0 || %s > 0 || %s > 0", 
			ATTR_TOTAL_RUNNING_JOBS, ATTR_TOTAL_IDLE_JOBS,
			ATTR_TOTAL_HELD_JOBS, ATTR_TOTAL_REMOVED_JOBS,
			ATTR_TOTAL_JOB_ADS );
		result = scheddQuery.addANDConstraint( constraint );
		if( result != Q_OK ) {
			fprintf( stderr, "Error: Couldn't add schedd-constraint %s\n", constraint);
			exit( 1 );
		}
	}

	// get the list of ads from the collector
	if( querySchedds ) { 
		result = Collectors->query ( scheddQuery, scheddList );
	} else {
		result = Collectors->query ( submittorQuery, scheddList );
	}

	switch( result ) {
	case Q_OK:
		break;
	case Q_COMMUNICATION_ERROR: 
			// if we're not an expert, we want verbose output
		printNoCollectorContact( stderr, pool ? pool->name() : NULL,
								 !expert ); 
		exit( 1 );
	case Q_NO_COLLECTOR_HOST:
		ASSERT( pool );
		fprintf( stderr, "Error: Can't contact condor_collector: "
				 "invalid hostname: %s\n", pool->name() );
		exit( 1 );
	default:
		fprintf( stderr, "Error fetching ads: %s\n", 
				 getStrQueryResult(result) );
		exit( 1 );
	}

		/*if(querySchedds && scheddList.MyLength() == 0) {
		  result = Collectors->query(quillQuery, quillList);
		}*/

	first = true;
	// get queue from each ScheddIpAddr in ad
	scheddList.Open();
	while ((ad = scheddList.Next()))
	{
		/* default to true for remotely queryable */
#ifdef HAVE_EXT_POSTGRESQL
		int flag=1;
		freeConnectionStrings();
#endif
		useDB = FALSE;
		if ( ! (ad->LookupString(ATTR_SCHEDD_IP_ADDR, &scheddAddr)  &&
				ad->LookupString(ATTR_NAME, &scheddName) &&
				ad->LookupString(ATTR_MACHINE, scheddMachine, sizeof(scheddMachine))
				)
			)
		{
			/* something is wrong with this schedd/quill ad, try the next one */
			continue;
		}

#ifdef HAVE_EXT_POSTGRESQL
		std::string daemonAdName;
			// get the address of the database
		if (ad->LookupString(ATTR_QUILL_DB_IP_ADDR, sdbIpAddr) &&
			ad->LookupString(ATTR_QUILL_NAME, squillName) &&
			ad->LookupString(ATTR_QUILL_DB_NAME, sdbName) && 
			ad->LookupString(ATTR_QUILL_DB_QUERY_PASSWORD, squeryPassword) &&
			(!ad->LookupBool(ATTR_QUILL_IS_REMOTELY_QUERYABLE,flag) || flag)) {

			dbIpAddr = sdbIpAddr.c_str();
			quillName = squillName.c_str();
			dbName = sdbName.c_str();
			queryPassword = squeryPassword.c_str();

			/* If the quill information is available, try to use it first */
			useDB = TRUE;
	    	if (ad->LookupString(ATTR_SCHEDD_NAME, daemonAdName)) {
				Q.addSchedd(daemonAdName.c_str());
			} else {
				Q.addSchedd(scheddName);
			}

				/* get the quill info for fail-over processing */
			ASSERT(ad->LookupString(ATTR_MACHINE, squillMachine));
			quillMachine = squillMachine.c_str();
		}
#endif 

		first = false;

#ifdef HAVE_EXT_POSTGRESQL
			/* check if direct DB query is indicated */
		if ( directDBquery ) {				
			if (!useDB) {
				printf ("\n\n-- Schedd: %s : %s\n", scheddName, scheddAddr);
				fprintf(stderr, "Database query not supported on schedd: %s\n",
						scheddName);
				continue;
			}
			
			exec_db_query(quillName, dbIpAddr, dbName, queryPassword);

			/* done processing the ad, so get the next one */
			continue;

		}
#endif /* HAVE_EXT_POSTGRESQL */

		init_output_mask();

		/* When an installation has database parameters configured, it means 
		   there is quill daemon. If database is not accessible, we fail
		   over to quill daemon, and if quill daemon is not available, 
		   we fail over the schedd daemon */			
		switch(direct)
		{
			case DIRECT_ALL:
				/* FALL THROUGH */
#ifdef HAVE_EXT_POSTGRESQL
			case DIRECT_RDBMS:
				if (useDB) {
					retval = show_db_queue(quillName, dbIpAddr, dbName, queryPassword);
					if (retval)
					{
						/* processed correctly, so do the next ad */
						continue;
					}

					fprintf( stderr, "-- Database server %s\n"
							"\tbeing used by the quill daemon %s\n"
							"\tassociated with schedd %s(%s)\n"
							"\tis not reachable or down.\n",
							dbIpAddr, quillName, scheddName, scheddAddr);

					if (direct == DIRECT_RDBMS) {
						fprintf(stderr, 
							"\t- Not failing over due to -direct\n");
						continue;
					} 

					fprintf(stderr, 
						"\t- Failing over to the quill daemon at %s--\n", 
						quillName);

				} else {
					if (direct == DIRECT_RDBMS) {
						fprintf(stderr, 
							"-- Direct query to rdbms on behalf of\n"
							"\tschedd %s(%s) failed.\n"
							"\t- Schedd doesn't appear to be using quill.\n",
							scheddName, scheddAddr);
						fprintf(stderr,
							"\t- Not failing over due to -direct\n");
						continue;
					}
				}

				/* FALL THROUGH */

			case DIRECT_QUILLD:
				if (useDB) {
					QueryResult result2 = getQuillAddrFromCollector(quillName, quillAddr);

					/* if quillAddr is NULL, then while the collector's 
						schedd's ad had a quill name in it, the collector
						didn't have a quill ad by that name. */

					if((result2 == Q_OK) && !quillAddr.empty() &&
					   (retval = show_schedd_queue(quillAddr.c_str(), quillName, quillMachine, 0))
					   )
					{
						/* processed correctly, so do the next ad */
						continue;
					}

					/* NOTE: it is not impossible that quillAddr could be
						NULL if the quill name is specified in a schedd ad
						but the collector has no record of such quill ad by
						that name. So we deal with that mess here in the 
						debugging output. */

					fprintf( stderr,
						"-- Quill daemon %s(%s)\n"
						"\tassociated with schedd %s(%s)\n"
						"\tis not reachable or can't talk to rdbms.\n",
						quillName, quillAddr.empty()?"<\?\?\?>":quillAddr.c_str(),
						scheddName, scheddAddr);

					if (quillAddr.empty()) {
						fprintf(stderr,
							"\t- Possible misconfiguration of quill\n"
							"\tassociated with this schedd since associated\n"
							"\tquilld ad is missing and was expected to be\n"
							"\tfound in the collector.\n");
					}

					if (direct == DIRECT_QUILLD) {
						fprintf(stderr,
							"\t- Not failing over due to use of -direct\n");
						continue;
					}

					fprintf(stderr,
						"\t- Failing over to the schedd at %s(%s) --\n",
						scheddName, scheddAddr);

				} else {
					if (direct == DIRECT_QUILLD) {
						fprintf(stderr,
							"-- Direct query to quilld associated with\n"
							"\tschedd %s(%s) failed.\n"
							"\t- Schedd doesn't appear to be using quill.\n",
							scheddName, scheddAddr);
						printf("\t- Not failing over due to use of -direct\n");
						continue;
					}
				}

				/* FALL THROUGH */

#endif /* HAVE_EXT_POSTGRESQL */

			case DIRECT_SCHEDD:
				/* database not configured or could not be reached,
					query the schedd daemon directly */
				{
				MyString scheddVersion;
				ad->LookupString(ATTR_VERSION, scheddVersion);
				CondorVersionInfo v(scheddVersion.Value());
				useFastScheddQuery = v.built_since_version(6,9,3) ? (v.built_since_version(8, 3, 3) ? 2 : 1) : 0;
				retval = show_schedd_queue(scheddAddr, scheddName, scheddMachine, useFastScheddQuery);
				}

				break;

			case DIRECT_UNKNOWN:
			default:
				fprintf( stderr,
					"-- Cannot determine any location for queue "
					"using option -direct!\n"
					"\t- This is an internal error and should be "
					"reported to condor-admin@cs.wisc.edu." );
				exit(EXIT_FAILURE);
				break;
		}
	}

	// close list
	scheddList.Close();

	if( first ) {
		if( global ) {
			printf( "All queues are empty\n" );
			retval = 1;
		} else {
			fprintf(stderr,"Error: Collector has no record of "
							"schedd/submitter\n");

			exit(1);
		}
	}

	exit(retval?EXIT_SUCCESS:EXIT_FAILURE);
}

// when exit is called, this is called also
static int cleanup_globals(int exit_code)
{
	// pass the exit code through dprintf_SetExitCode so that it knows
	// whether to print out the on-error buffer or not.
	dprintf_SetExitCode(exit_code);

#ifdef HAVE_EXT_POSTGRESQL
	quillCleanupOnExit(exit_code);
#endif

	// do this to make Valgrind happy.
	startdAds.Clear();
	scheddList.Clear();

	return exit_code;
}

// append all variable references made by expression to references list
static bool
GetAllReferencesFromClassAdExpr(char const *expression,StringList &references)
{
	ClassAd ad;
	return ad.GetExprReferences(expression,NULL,&references);
}

static int
parse_analyze_detail(const char * pch, int current_details)
{
	//PRAGMA_REMIND("TJ: analyze_detail_level should be enum rather than integer")
	int details = 0;
	int flg = 0;
	while (int ch = *pch) {
		++pch;
		if (ch >= '0' && ch <= '9') {
			flg *= 10;
			flg += ch - '0';
		} else if (ch == ',' || ch == '+') {
			details |= flg;
			flg = 0;
		}
	}
	if (0 == (details | flg))
		return 0;
	return current_details | details | flg;
}

static void 
processCommandLineArguments (int argc, char *argv[])
{
	int i;
	char *at, *daemonname;
	const char * pcolon;

	bool custom_attributes = false;
	attrs.initializeFromString(
		"ClusterId\nProcId\nQDate\nRemoteUserCPU\nJobStatus\nServerTime\nShadowBday\n"
		"RemoteWallClockTime\nJobPrio\nImageSize\nOwner\nCmd\nArgs\nArguments\n"
		"JobDescription\nMATCH_EXP_JobDescription\nTransferringInput\nTransferringOutput\nTransferQueued");

	for (i = 1; i < argc; i++)
	{
		// no dash means this arg is a cluster/proc, proc, or owner
		if( *argv[i] != '-' ) {
			int cluster, proc;
			const char * pend;
			if (StrIsProcId(argv[i], cluster, proc, &pend) && *pend == 0) {
				constrID.push_back(CondorID(cluster,proc,-1));
			}
			else {
				++cOwnersOnCmdline;
				if( Q.add( CQ_OWNER, argv[i] ) != Q_OK ) {
					// this error doesn't seem very helpful... can't we say more?
					fprintf( stderr, "Error: Argument %d (%s) must be a jobid or user\n", i, argv[i] );
					exit( 1 );
				}
			}

			continue;
		}

		// the argument began with a '-', so use only the part after
		// the '-' for prefix matches
		const char* dash_arg = argv[i];

		if (is_dash_arg_colon_prefix (dash_arg, "wide", &pcolon, 1)) {
			widescreen = true;
			if (pcolon) {
				testing_width = atoi(++pcolon);
				if ( ! mask.IsEmpty()) mask.SetOverallWidth(getDisplayWidth()-1);
				if (testing_width <= 80) widescreen = false;
			}
			continue;
		}
		if (is_dash_arg_prefix (dash_arg, "long", 1)) {
			dash_long = 1;
			summarize = 0;
		} 
		else
		if (is_dash_arg_prefix (dash_arg, "xml", 3)) {
			use_xml = 1;
			dash_long = 1;
			summarize = 0;
			customHeadFoot = HF_BARE;
		}
		else
		if (is_dash_arg_prefix (dash_arg, "limit", 3)) {
			if (++i >= argc) {
				fprintf(stderr, "Error: -limit requires the max number of results as an argument.\n");
				exit(1);
			}
			char *endptr;
			g_match_limit = strtol(argv[i], &endptr, 10);
			if (*endptr != '\0')
			{
				fprintf(stderr, "Error: Unable to convert (%s) to a number for -limit.\n", argv[i]);
				exit(1);
			}
			if (g_match_limit <= 0)
			{
				fprintf(stderr, "Error: %d is not a valid for -limit.\n", g_match_limit);
				exit(1);
			}
		}
		else
		if (is_dash_arg_prefix (dash_arg, "pool", 1)) {
			if( pool ) {
				delete pool;
			}
			if( ++i >= argc ) {
				fprintf( stderr,
						 "Error: -pool requires a hostname as an argument.\n" );
				if (!expert) {
					printf("\n");
					print_wrapped_text("Extra Info: The hostname should be the central "
									   "manager of the Condor pool you wish to work with.",
									   stderr);
				}
				exit(1);
			}
			pool = new DCCollector( argv[i] );
			if( ! pool->addr() ) {
				fprintf( stderr, "Error: %s\n", pool->error() );
				if (!expert) {
					printf("\n");
					print_wrapped_text("Extra Info: You specified a hostname for a pool "
									   "(the -pool argument). That should be the Internet "
									   "host name for the central manager of the pool, "
									   "but it does not seem to "
									   "be a valid hostname. (The DNS lookup failed.)",
									   stderr);
				}
				exit(1);
			}
			Collectors = new CollectorList();
				// Add a copy of our DCCollector object, because
				// CollectorList() takes ownership and may even delete
				// this object before we are done.
			Collectors->append ( new DCCollector( *pool ) );
		} 
		else
		if (is_arg_prefix (dash_arg+1, "D", 1)) {
			if( ++i >= argc ) {
				fprintf( stderr, 
						 "Error: %s requires a list of flags as an argument.\n", dash_arg );
				if (!expert) {
					printf("\n");
					print_wrapped_text("Extra Info: You need to specify debug flags "
									   "as a quoted string. Common flags are D_ALL, and "
									   "D_FULLDEBUG.",
									   stderr);
				}
				exit( 1 );
			}
			set_debug_flags( argv[i], 0 );
		} 
		else
		if (is_dash_arg_prefix (dash_arg, "name", 1)) {

			if (querySubmittors) {
				// cannot query both schedd's and submittors
				fprintf (stderr, "Cannot query both schedd's and submitters\n");
				if (!expert) {
					printf("\n");
					print_wrapped_text("Extra Info: You cannot specify both -name and "
									   "-submitter. -name implies you want to only query "
									   "the local schedd, while -submitter implies you want "
									   "to find everything in the entire pool for a given"
									   "submitter.",
									   stderr);
				}
				exit(1);
			}

			// make sure we have at least one more argument
			if (argc <= i+1) {
				fprintf( stderr, 
						 "Error: -name requires the name of a schedd as an argument.\n" );
				exit(1);
			}

			if( !(daemonname = get_daemon_name(argv[i+1])) ) {
				fprintf( stderr, "Error: unknown host %s\n",
						 get_host_part(argv[i+1]) );
				if (!expert) {
					printf("\n");
					print_wrapped_text("Extra Info: The name given with the -name "
									   "should be the name of a condor_schedd process. "
									   "Normally it is either a hostname, or "
									   "\"name@hostname\". "
									   "In either case, the hostname should be the Internet "
									   "host name, but it appears that it wasn't.",
									   stderr);
				}
				exit(1);
			}
			sprintf (constraint, "%s == \"%s\"", ATTR_NAME, daemonname);
			scheddQuery.addORConstraint (constraint);
			Q.addSchedd(daemonname);

#ifdef HAVE_EXT_POSTGRESQL
			sprintf (constraint, "%s == \"%s\"", ATTR_QUILL_NAME, daemonname);
			scheddQuery.addORConstraint (constraint);
#endif

			delete [] daemonname;
			i++;
			querySchedds = true;
		} 
		else
		if (is_dash_arg_prefix (dash_arg, "direct", 1)) {
			/* check for one more argument */
			if (argc <= i+1) {
				fprintf( stderr, 
					"Error: -direct requires ["
					#ifdef HAVE_EXT_POSTGRESQL
						"rdbms | quilld | "
					#endif
						"schedd]\n" );
				exit(EXIT_FAILURE);
			}
			direct = process_direct_argument(argv[i+1]);
			i++;
		}
		else
		if (is_dash_arg_prefix (dash_arg, "submitter", 1)) {

			if (querySchedds) {
				// cannot query both schedd's and submittors
				fprintf (stderr, "Cannot query both schedd's and submitters\n");
				if (!expert) {
					printf("\n");
					print_wrapped_text("Extra Info: You cannot specify both -name and "
									   "-submitter. -name implies you want to only query "
									   "the local schedd, while -submitter implies you want "
									   "to find everything in the entire pool for a given"
									   "submitter.",
									   stderr);
				}
				exit(1);
			}
			
			// make sure we have at least one more argument
			if (argc <= i+1) {
				fprintf( stderr, "Error: -submitter requires the name of a user.\n");
				exit(1);
			}
				
			i++;
			if ((at = strchr(argv[i],'@'))) {
				// is the name already qualified with a UID_DOMAIN?
				sprintf (constraint, "%s == \"%s\"", ATTR_NAME, argv[i]);
				*at = '\0';
			} else {
				// no ... add UID_DOMAIN
				char *uid_domain = param( "UID_DOMAIN" );
				if (uid_domain == NULL)
				{
					EXCEPT ("No 'UID_DOMAIN' found in config file");
				}
				sprintf (constraint, "%s == \"%s@%s\"", ATTR_NAME, argv[i], 
							uid_domain);
				free (uid_domain);
			}

			// insert the constraints
			submittorQuery.addORConstraint (constraint);

			{
				char *ownerName = argv[i];
				// ensure that the "nice-user" prefix isn't inserted as part
				// of the job ad constraint
				if( strstr( argv[i] , NiceUserName ) == argv[i] ) {
					ownerName = argv[i]+strlen(NiceUserName)+1;
				}
				// ensure that the group prefix isn't inserted as part
				// of the job ad constraint.
				char *groups = param("GROUP_NAMES");
				if ( groups && ownerName ) {
					StringList groupList(groups);
					char *dotptr = strchr(ownerName,'.');
					if ( dotptr ) {
						*dotptr = '\0';
						if ( groupList.contains_anycase(ownerName) ) {
							// this name starts with a group prefix.
							// strip it off.
							ownerName = dotptr + 1;	// add one for the '.'
						}
						*dotptr = '.';
					}
				}
				if ( groups ) free(groups);
				if (Q.add (CQ_OWNER, ownerName) != Q_OK) {
					fprintf (stderr, "Error:  Argument %d (%s)\n", i, argv[i]);
					exit (1);
				}
			}

			querySubmittors = true;
		}
		else
		if (is_dash_arg_prefix (dash_arg, "constraint", 1)) {
			// make sure we have at least one more argument
			if (argc <= i+1) {
				fprintf( stderr, "Error: -constraint requires another parameter\n");
				exit(1);
			}
			user_job_constraint = argv[++i];

			if (Q.addAND (user_job_constraint) != Q_OK) {
				fprintf (stderr, "Error: Argument %d (%s) is not a valid constraint\n", i, user_job_constraint);
				exit (1);
			}
		} 
		else
		if (is_dash_arg_prefix (dash_arg, "slotconstraint", 5) || is_dash_arg_prefix (dash_arg, "mconstraint", 2)) {
			// make sure we have at least one more argument
			if (argc <= i+1) {
				fprintf( stderr, "Error: %s requires another parameter\n", dash_arg);
				exit(1);
			}
			user_slot_constraint = argv[++i];
		}
		else
		if (is_dash_arg_prefix (dash_arg, "machine", 2)) {
			// make sure we have at least one more argument
			if (argc <= i+1) {
				fprintf( stderr, "Error: %s requires another parameter\n", dash_arg);
				exit(1);
			}
			user_slot_constraint = argv[++i];
		}
		else
		if (is_dash_arg_prefix (dash_arg, "address", 1)) {

			if (querySubmittors) {
				// cannot query both schedd's and submittors
				fprintf (stderr, "Cannot query both schedd's and submitters\n");
				exit(1);
			}

			// make sure we have at least one more argument
			if (argc <= i+1) {
				fprintf( stderr,
						 "Error: -address requires another parameter\n" );
				exit(1);
			}
			if( ! is_valid_sinful(argv[i+1]) ) {
				fprintf( stderr, 
					 "Address must be of the form: \"<ip.address:port>\"\n" );
				exit(1);
			}
			sprintf(constraint, "%s == \"%s\"", ATTR_SCHEDD_IP_ADDR, argv[i+1]);
			scheddQuery.addORConstraint(constraint);
			i++;
			querySchedds = true;
		} 
		else
		if (is_dash_arg_colon_prefix (dash_arg, "autocluster", &pcolon, 2)) {
			dash_autocluster = CondorQ::fetch_DefaultAutoCluster;
		}
		else
		if (is_dash_arg_colon_prefix (dash_arg, "group-by", &pcolon, 2)) {
			dash_autocluster = CondorQ::fetch_GroupBy;
		}
		else
		if (is_dash_arg_prefix (dash_arg, "attributes", 2)) {
			if( argc <= i+1 ) {
				fprintf( stderr, "Error: -attributes requires a list of attributes to show\n" );
				exit( 1 );
			}
			if( !custom_attributes ) {
				custom_attributes = true;
				attrs.clearAll();
			}
			StringList more_attrs(argv[i+1],",");
			char const *s;
			more_attrs.rewind();
			while( (s=more_attrs.next()) ) {
				attrs.append(s);
			}
			i++;
		}
		else
		if (is_dash_arg_prefix (dash_arg, "format", 1)) {
				// make sure we have at least two more arguments
			if( argc <= i+2 ) {
				fprintf( stderr, "Error: -format requires format and attribute parameters\n" );
				exit( 1 );
			}
			if( !custom_attributes ) {
				custom_attributes = true;
				attrs.clearAll();
				if ( ! dash_autocluster) {
					attrs.initializeFromString("ClusterId\nProcId"); // this is needed to prevent some DAG code from faulting.
				}
			}
			GetAllReferencesFromClassAdExpr(argv[i+2],attrs);
				
			customHeadFoot = HF_BARE;
			mask.registerFormat( argv[i+1], argv[i+2] );
			usingPrintMask = true;
			i+=2;
		}
		else
		if (is_dash_arg_colon_prefix(dash_arg, "autoformat", &pcolon, 5) ||
			is_dash_arg_colon_prefix(dash_arg, "af", &pcolon, 2)) {
				// make sure we have at least one more argument
			if ( (i+1 >= argc)  || *(argv[i+1]) == '-') {
				fprintf( stderr, "Error: -autoformat requires at last one attribute parameter\n" );
				exit( 1 );
			}
			if( !custom_attributes ) {
				custom_attributes = true;
				attrs.clearAll();
				if ( ! dash_autocluster) {
					attrs.initializeFromString("ClusterId\nProcId"); // this is needed to prevent some DAG code from faulting.
				}
			}
			bool flabel = false;
			bool fCapV  = false;
			bool fheadings = false;
			bool fJobId = false;
			bool fRaw = false;
			const char * prowpre = NULL;
			const char * pcolpre = " ";
			const char * pcolsux = NULL;
			if (pcolon) {
				++pcolon;
				while (*pcolon) {
					switch (*pcolon)
					{
						case ',': pcolsux = ","; break;
						case 'n': pcolsux = "\n"; break;
						case 'g': pcolpre = NULL; prowpre = "\n"; break;
						case 't': pcolpre = "\t"; break;
						case 'l': flabel = true; break;
						case 'V': fCapV = true; break;
						case 'r': case 'o': fRaw = true; break;
						case 'h': fheadings = true; break;
						case 'j': fJobId = true; break;
					}
					++pcolon;
				}
			}
			if (fJobId) {
				if (fheadings || mask.has_headings()) {
					mask.set_heading(" ID");
					mask.registerFormat (flabel ? "ID = %4d." : "%4d.", 5, FormatOptionAutoWidth | FormatOptionNoSuffix, ATTR_CLUSTER_ID);
					mask.set_heading(" ");
					mask.registerFormat ("%-3d", 3, FormatOptionAutoWidth | FormatOptionNoPrefix, ATTR_PROC_ID);
				} else {
					mask.registerFormat (flabel ? "ID = %d." : "%d.", 0, FormatOptionNoSuffix, ATTR_CLUSTER_ID);
					mask.registerFormat ("%d", 0, FormatOptionNoPrefix, ATTR_PROC_ID);
				}
			}
			// process all arguments that don't begin with "-" as part
			// of autoformat.
			while (i+1 < argc && *(argv[i+1]) != '-') {
				++i;
				GetAllReferencesFromClassAdExpr(argv[i], attrs);
				MyString lbl = "";
				int wid = 0;
				int opts = FormatOptionNoTruncate;
				if (fheadings || mask.has_headings()) {
					const char * hd = fheadings ? argv[i] : "(expr)";
					wid = 0 - (int)strlen(hd);
					opts = FormatOptionAutoWidth | FormatOptionNoTruncate; 
					mask.set_heading(hd);
				}
				else if (flabel) { lbl.formatstr("%s = ", argv[i]); wid = 0; opts = 0; }
				lbl += fRaw ? "%r" : (fCapV ? "%V" : "%v");
				mask.registerFormat(lbl.Value(), wid, opts, argv[i]);
			}
			mask.SetAutoSep(prowpre, pcolpre, pcolsux, "\n");
			customHeadFoot = HF_BARE;
			if (fheadings) { customHeadFoot = (printmask_headerfooter_t)(customHeadFoot & ~HF_NOHEADER); }
			usingPrintMask = true;
			// if autoformat list ends in a '-' without any characters after it, just eat the arg and keep going.
			if (i+1 < argc && '-' == (argv[i+1])[0] && 0 == (argv[i+1])[1]) {
				++i;
			}
		}
		else
		if (is_dash_arg_colon_prefix(argv[i], "print-format", &pcolon, 2)) {
			if ( (i+1 >= argc)  || (*(argv[i+1]) == '-' && (argv[i+1])[1] != 0)) {
				fprintf( stderr, "Error: -print-format requires a filename argument\n");
				exit( 1 );
			}
			// hack allow -pr ! to disable use of user-default print format files.
			if (MATCH == strcmp(argv[i+1], "!")) {
				++i;
				disable_user_print_files = true;
				continue;
			}
			if ( ! widescreen) mask.SetOverallWidth(getDisplayWidth()-1);
			if( !custom_attributes ) {
				custom_attributes = true;
				attrs.clearAll();
			}
			++i;
			if (set_print_mask_from_stream(argv[i], true, attrs) < 0) {
				fprintf(stderr, "Error: invalid select file %s\n", argv[i]);
				exit (1);
			}
		}
		else
		if (is_dash_arg_prefix (dash_arg, "global", 1)) {
			global = 1;
		} 
		else
		if (is_dash_arg_prefix (dash_arg, "schedd-constraint", 5)) {
			// make sure we have at least one more argument
			if (argc <= i+1) {
				fprintf( stderr, "Error: -schedd-constraint requires a constraint argument\n");
				exit(1);
			}
			global_schedd_constraint = argv[++i];

			if (scheddQuery.addANDConstraint (global_schedd_constraint) != Q_OK) {
				fprintf (stderr, "Error: Invalid constraint (%s)\n", global_schedd_constraint);
				exit (1);
			}
			global = 1;
		}
		else
		if (is_dash_arg_prefix (argv[i], "help", 1)) {
			int other = 0;
			while ((i+1 < argc) && *(argv[i+1]) != '-') {
				++i;
				if (is_arg_prefix(argv[i], "universe", 3)) {
					other |= usage_Universe;
				} else if (is_arg_prefix(argv[i], "state", 2) || is_arg_prefix(argv[i], "status", 2)) {
					other |= usage_JobStatus;
				} else if (is_arg_prefix(argv[i], "all", 2)) {
					other |= usage_AllOther;
				}
			}
			usage(argv[0], other);
			exit(0);
		}
		else
		if (is_dash_arg_colon_prefix(dash_arg, "better-analyze", &pcolon, 2)
			|| is_dash_arg_colon_prefix(dash_arg, "better-analyse", &pcolon, 2)
			|| is_dash_arg_colon_prefix(dash_arg, "analyze", &pcolon, 2)
			|| is_dash_arg_colon_prefix(dash_arg, "analyse", &pcolon, 2)
			) {
			better_analyze = true;
			if (dash_arg[1] == 'b' || dash_arg[2] == 'b') { // if better, default to higher verbosity output.
				analyze_detail_level |= detail_better | detail_analyze_each_sub_expr | detail_always_analyze_req;
			}
			if (pcolon) { 
				StringList opts(++pcolon, ",:");
				opts.rewind();
				while(const char *popt = opts.next()) {
					//printf("parsing opt='%s'\n", popt);
					if (is_arg_prefix(popt, "summary",1)) {
						summarize_anal = true;
						analyze_with_userprio = false;
					} else if (is_arg_prefix(popt, "reverse",1)) {
						reverse_analyze = true;
						analyze_with_userprio = false;
					} else if (is_arg_prefix(popt, "priority",2)) {
						analyze_with_userprio = true;
					} else if (is_arg_prefix(popt, "noprune",3)) {
						analyze_detail_level |= detail_show_all_subexprs;
					} else if (is_arg_prefix(popt, "diagnostic",4)) {
						analyze_detail_level |= detail_diagnostic;
					} else if (is_arg_prefix(popt, "memory",3)) {
						analyze_memory_usage = opts.next();
						if (analyze_memory_usage) { analyze_memory_usage = strdup(analyze_memory_usage); }
						else { analyze_memory_usage = ATTR_REQUIREMENTS; }
					//} else if (is_arg_prefix(popt, "dslots",2)) {
					//	analyze_dslots = true;
					} else {
						analyze_detail_level = parse_analyze_detail(popt, analyze_detail_level);
					}
				}
			}
			attrs.clearAll();
		}
		else
		if (is_dash_arg_colon_prefix(dash_arg, "reverse", &pcolon, 3)) {
			reverse_analyze = true; // modify analyze to be reverse analysis
			if (pcolon) { 
				analyze_detail_level |= parse_analyze_detail(++pcolon, analyze_detail_level);
			}
		}
		else
		if (is_dash_arg_colon_prefix(dash_arg, "reverse-analyze", &pcolon, 10)) {
			reverse_analyze = true; // modify analyze to be reverse analysis
			better_analyze = true;	// enable analysis
			analyze_with_userprio = false;
			if (pcolon) { 
				analyze_detail_level |= parse_analyze_detail(++pcolon, analyze_detail_level);
			}
			attrs.clearAll();
		}
		else
		if (is_dash_arg_prefix(dash_arg, "verbose", 4)) {
			// chatty output, mostly for for -analyze
			// this is not the same as -long. 
			verbose = true;
		}
		else
		if (is_dash_arg_prefix(dash_arg, "run", 1)) {
			std::string expr;
			formatstr( expr, "%s == %d || %s == %d || %s == %d", ATTR_JOB_STATUS, RUNNING,
					 ATTR_JOB_STATUS, TRANSFERRING_OUTPUT, ATTR_JOB_STATUS, SUSPENDED );
			Q.addAND( expr.c_str() );
			dash_run = true;
			if( !dash_dag ) {
				attrs.append( ATTR_REMOTE_HOST );
				attrs.append( ATTR_JOB_UNIVERSE );
				attrs.append( ATTR_EC2_REMOTE_VM_NAME ); // for displaying HOST(s) in EC2
			}
		}
		else
		if (is_dash_arg_prefix(dash_arg, "hold", 2) || is_dash_arg_prefix(dash_arg, "held", 2)) {
			Q.add (CQ_STATUS, HELD);		
			show_held = true;
			widescreen = true;
			attrs.append( ATTR_ENTERED_CURRENT_STATUS );
			attrs.append( ATTR_HOLD_REASON );
		}
		else
		if (is_dash_arg_prefix(dash_arg, "goodput", 2)) {
			// goodput and show_io require the same column
			// real-estate, so they're mutually exclusive
			dash_goodput = true;
			show_io = false;
			attrs.append( ATTR_JOB_COMMITTED_TIME );
			attrs.append( ATTR_SHADOW_BIRTHDATE );
			attrs.append( ATTR_LAST_CKPT_TIME );
			attrs.append( ATTR_JOB_REMOTE_WALL_CLOCK );
		}
		else
		if (is_dash_arg_prefix(dash_arg, "cputime", 2)) {
			cputime = true;
			JOB_TIME = "CPU_TIME";
		 	attrs.append( ATTR_JOB_REMOTE_USER_CPU );
		}
		else
		if (is_dash_arg_prefix(dash_arg, "currentrun", 2)) {
			current_run = true;
		}
		else
		if (is_dash_arg_prefix(dash_arg, "grid", 2 )) {
			// grid is a superset of globus, so we can't do grid if globus has been specifed
			if ( ! dash_globus) {
				dash_grid = true;
				attrs.append( ATTR_GRID_JOB_ID );
				attrs.append( ATTR_GRID_RESOURCE );
				attrs.append( ATTR_GRID_JOB_STATUS );
				attrs.append( ATTR_GLOBUS_STATUS );
    			attrs.append( ATTR_EC2_REMOTE_VM_NAME );
				Q.addAND( "JobUniverse == 9" );
			}
		}
		else
		if (is_dash_arg_prefix(dash_arg, "globus", 5 )) {
			Q.addAND( "GlobusStatus =!= UNDEFINED" );
			dash_globus = true;
			if (dash_grid) {
				dash_grid = false;
			} else {
				attrs.append( ATTR_GLOBUS_STATUS );
				attrs.append( ATTR_GRID_RESOURCE );
				attrs.append( ATTR_GRID_JOB_STATUS );
				attrs.append( ATTR_GRID_JOB_ID );
			}
		}
		else
		if (is_dash_arg_colon_prefix(dash_arg, "debug", &pcolon, 3)) {
			// dprintf to console
			dprintf_set_tool_debug("TOOL", 0);
			if (pcolon && pcolon[1]) {
				set_debug_flags( ++pcolon, 0 );
			}
		}
		else
		if (is_dash_arg_prefix(dash_arg, "io", 2)) {
			// goodput and show_io require the same column
			// real-estate, so they're mutually exclusive
			show_io = true;
			dash_goodput = false;
			attrs.append(ATTR_NUM_JOB_STARTS);
			attrs.append(ATTR_NUM_CKPTS_RAW);
			attrs.append(ATTR_FILE_READ_BYTES);
			attrs.append(ATTR_BYTES_RECVD);
			attrs.append(ATTR_FILE_WRITE_BYTES);
			attrs.append(ATTR_BYTES_SENT);
			attrs.append(ATTR_JOB_REMOTE_WALL_CLOCK);
			attrs.append(ATTR_FILE_SEEK_COUNT);
			attrs.append(ATTR_BUFFER_SIZE);
			attrs.append(ATTR_BUFFER_BLOCK_SIZE);
			attrs.append(ATTR_TRANSFERRING_INPUT);
			attrs.append(ATTR_TRANSFERRING_OUTPUT);
			attrs.append(ATTR_TRANSFER_QUEUED);
		}
		else if (is_dash_arg_prefix(dash_arg, "dag", 2)) {
			dash_dag = true;
			attrs.clearAll();
			if( g_stream_results  ) {
				fprintf( stderr, "-stream-results and -dag are incompatible\n" );
				usage( argv[0] );
				exit( 1 );
			}
		}
		else if (is_dash_arg_prefix(dash_arg, "totals", 3)) {
			if( !custom_attributes ) {
				custom_attributes = true;
				attrs.clearAll();
			}
			if (set_print_mask_from_stream("SELECT NOHEADER\nSUMMARY STANDARD", false, attrs) < 0) {
				fprintf(stderr, "Error: unexpected error!\n");
				exit (1);
			}
		}
		else if (is_dash_arg_prefix(dash_arg, "expert", 1)) {
			expert = true;
			attrs.clearAll();
		}
		else if (is_dash_arg_prefix(dash_arg, "jobads", 1)) {
			if (argc <= i+1) {
				fprintf( stderr, "Error: -jobads requires a filename\n");
				exit(1);
			} else {
				i++;
				jobads_file = argv[i];
			}
		}
		else if (is_dash_arg_prefix(dash_arg, "userlog", 1)) {
			if (argc <= i+1) {
				fprintf( stderr, "Error: -userlog requires a filename\n");
				exit(1);
			} else {
				i++;
				userlog_file = argv[i];
			}
		}
		else if (is_dash_arg_prefix(dash_arg, "slotads", 1)) {
			if (argc <= i+1) {
				fprintf( stderr, "Error: -slotads requires a filename\n");
				exit(1);
			} else {
				i++;
				machineads_file = argv[i];
			}
		}
		else if (is_dash_arg_colon_prefix(dash_arg, "userprios", &pcolon, 5)) {
			if (argc <= i+1) {
				fprintf( stderr, "Error: -userprios requires a filename argument\n");
				exit(1);
			} else {
				i++;
				userprios_file = argv[i];
				analyze_with_userprio = true;
			}
		}
		else if (is_dash_arg_colon_prefix(dash_arg, "nouserprios", &pcolon, 7)) {
			analyze_with_userprio = false;
		}
#ifdef HAVE_EXT_POSTGRESQL
		else if (is_dash_arg_prefix(dash_arg, "avgqueuetime", 4)) {
				/* if user want average wait time, we will perform direct DB query */
			avgqueuetime = true;
			directDBquery =  true;
		}
#endif /* HAVE_EXT_POSTGRESQL */
        else if (is_dash_arg_prefix(dash_arg, "version", 1)) {
			printf( "%s\n%s\n", CondorVersion(), CondorPlatform() );
			exit(0);
        }
		else if (is_dash_arg_colon_prefix(dash_arg, "profile", &pcolon, 4)) {
			dash_profile = true;
			if (pcolon) {
				StringList opts(++pcolon, ",:");
				opts.rewind();
				while (const char * popt = opts.next()) {
					if (is_arg_prefix(popt, "on", 2)) {
						classad::ClassAdSetExpressionCaching(true);
					} else if (is_arg_prefix(popt, "off", 2)) {
						classad::ClassAdSetExpressionCaching(false);
					}
				}
			}
		}
		else
		if (is_dash_arg_prefix (dash_arg, "stream-results", 2)) {
			g_stream_results = true;
			if( dash_dag ) {
				fprintf( stderr, "-stream-results and -dag are incompatible\n" );
				usage( argv[0] );
				exit( 1 );
			}
		}
		else {
			fprintf( stderr, "Error: unrecognized argument %s\n", dash_arg );
			usage(argv[0]);
			exit( 1 );
		}
	}

		//Added so -long or -xml can be listed before other options
	if(dash_long && !custom_attributes) {
		attrs.clearAll();
	}

	// convert cluster and cluster.proc into constraints
	// if there is a -dag argument, then we look up all children of the dag
	// as well as the dag itself.
	if ( ! constrID.empty()) {
		for (std::vector<CondorID>::const_iterator it = constrID.begin(); it != constrID.end(); ++it) {

			// if we aren't doing db queries, do we need to do this?
			Q.addDBConstraint(CQ_CLUSTER_ID, it->_cluster);
			if (it->_proc >= 0) { Q.addDBConstraint(CQ_PROC_ID, it->_proc); }

			// add a constraint to match the jobid.
			if (it->_proc >= 0) {
				sprintf(constraint, ATTR_CLUSTER_ID " == %d && " ATTR_PROC_ID " == %d", it->_cluster, it->_proc);
			} else {
				sprintf(constraint, ATTR_CLUSTER_ID " == %d", it->_cluster);
			}
			Q.addOR(constraint);

			// if we are doing -dag output, then also request any jobs that are inside this dag.
			// we know that a jobid for a dagman job will always never have a proc > 0
			if (dash_dag && it->_proc < 1) {
				sprintf(constraint, ATTR_DAGMAN_JOB_ID " == %d", it->_cluster);
				Q.addOR(constraint);
			}
		}
	} else if (show_io) {
		// if no job id's specifed, then constrain the query to jobs that are doing file transfer
		Q.addAND("JobUniverse==1 || TransferQueued=?=true || TransferringOutput=?=true || TransferringInput=?=true");
	}
}

static double
job_time(double cpu_time,ClassAd *ad)
{
	if ( cputime ) {
		return cpu_time;
	}

		// here user wants total wall clock time, not cpu time
	int job_status = 0;
	int cur_time = 0;
	int shadow_bday = 0;
	double previous_runs = 0;

	ad->LookupInteger( ATTR_JOB_STATUS, job_status);
	ad->LookupInteger( ATTR_SERVER_TIME, cur_time);
	ad->LookupInteger( ATTR_SHADOW_BIRTHDATE, shadow_bday );
	if ( current_run == false ) {
		ad->LookupFloat( ATTR_JOB_REMOTE_WALL_CLOCK, previous_runs );
	}

		// if we have an old schedd, there is no ATTR_SERVER_TIME,
		// so return a "-1".  This will cause "?????" to show up
		// in condor_q.
	if ( cur_time == 0 ) {
		return -1;
	}

	/* Compute total wall time as follows:  previous_runs is not the 
	 * number of seconds accumulated on earlier runs.  cur_time is the
	 * time from the point of view of the schedd, and shadow_bday is the
	 * epoch time from the schedd's view when the shadow was started for
	 * this job.  So, we figure out the time accumulated on this run by
	 * computing the time elapsed between cur_time & shadow_bday.  
	 * NOTE: shadow_bday is set to zero when the job is RUNNING but the
	 * shadow has not yet started due to JOB_START_DELAY parameter.  And
	 * shadow_bday is undefined (stale value) if the job status is not
	 * RUNNING.  So we only compute the time on this run if shadow_bday
	 * is not zero and the job status is RUNNING.  -Todd <tannenba@cs.wisc.edu>
	 */
	double total_wall_time = previous_runs;
	if ( ( job_status == RUNNING || job_status == TRANSFERRING_OUTPUT || job_status == SUSPENDED) && shadow_bday ) {
		total_wall_time += cur_time - shadow_bday;
	}

	return total_wall_time;
}

unsigned int process_direct_argument(char *arg)
{
#ifdef HAVE_EXT_POSTGRESQL
	if (strcasecmp(arg, "rdbms") == MATCH) {
		return DIRECT_RDBMS;
	}

	if (strcasecmp(arg, "quilld") == MATCH) {
		return DIRECT_QUILLD;
	}

#endif

	if (strcasecmp(arg, "schedd") == MATCH) {
		return DIRECT_SCHEDD;
	}

#ifdef HAVE_EXT_POSTGRESQL
	fprintf( stderr, 
		"Error: -direct requires [rdbms | quilld | schedd]\n" );
#else
	fprintf( stderr, 
		"Error: Quill feature set is not available.\n"
		"-direct may only take 'schedd' as an option.\n" );
#endif

	exit(EXIT_FAILURE);

	/* Here to make the compiler happy since there is an exit above */
	return DIRECT_UNKNOWN;
}


static void
buffer_io_display(std::string & out, ClassAd *ad)
{
	int univ=0;
	ad->EvalInteger(ATTR_JOB_UNIVERSE, NULL, univ);

	const char * idIter = ATTR_NUM_JOB_STARTS;
	if (univ==CONDOR_UNIVERSE_STANDARD) idIter = ATTR_NUM_CKPTS_RAW;

	int cluster=0, proc=0, iter=0, status=0;
	char misc[256];
	ad->LookupInteger(ATTR_CLUSTER_ID,cluster);
	ad->LookupInteger(ATTR_PROC_ID,proc);
	ad->LookupInteger(idIter,iter);
	ad->LookupString(ATTR_OWNER, misc, sizeof(misc));
	ad->LookupInteger(ATTR_JOB_STATUS,status);

	formatstr( out, "%4d.%-3d %-14s %4d %2s", cluster, proc, 
				format_owner( misc, ad ),
				iter,
				format_job_status_char(status, ad, *((Formatter*)NULL)) );

	double read_bytes=0, write_bytes=0, wall_clock=-1;
	ad->EvalFloat(ATTR_JOB_REMOTE_WALL_CLOCK,NULL,wall_clock);

	misc[0]=0;

	bool have_bytes = true;
	if (univ==CONDOR_UNIVERSE_STANDARD) {

		ad->EvalFloat(ATTR_FILE_READ_BYTES,NULL,read_bytes);
		ad->EvalFloat(ATTR_FILE_WRITE_BYTES,NULL,write_bytes);

		have_bytes = wall_clock >= 1.0 && (read_bytes >= 1.0 || write_bytes >= 1.0);

		double seek_count=0;
		int buffer_size=0, block_size=0;
		ad->EvalFloat(ATTR_FILE_SEEK_COUNT,NULL,seek_count);
		ad->EvalInteger(ATTR_BUFFER_SIZE,NULL,buffer_size);
		ad->EvalInteger(ATTR_BUFFER_BLOCK_SIZE,NULL,block_size);

		sprintf(misc, " seeks=%d, buf=%d,%d", (int)seek_count, buffer_size, block_size);
	} else {
		// not std universe. show transfer queue stats
		/*
			BytesRecvd/1048576.0 AS " MB_INPUT"  PRINTF "%9.2f"
			BytesSent/1048576.0  AS "MB_OUTPUT"  PRINTF "%9.2f"
			{JobStatus,TransferringInput,TransferringOutput,TransferQueued}[0] AS ST PRINTAS JOB_STATUS
			TransferQueued       AS XFER_Q
			TransferringInput    AS XFER_IN
			TransferringOutput=?=true || JobStatus==6   AS XFER_OUT
		*/
		ad->EvalFloat(ATTR_BYTES_RECVD,NULL,read_bytes);
		ad->EvalFloat(ATTR_BYTES_SENT,NULL,write_bytes);

		have_bytes = (read_bytes > 0 || write_bytes > 0);

		int transferring_input = false;
		int transferring_output = false;
		int transfer_queued = false;
		ad->EvalBool(ATTR_TRANSFERRING_INPUT,NULL,transferring_input);
		ad->EvalBool(ATTR_TRANSFERRING_OUTPUT,NULL,transferring_output);
		ad->EvalBool(ATTR_TRANSFER_QUEUED,NULL,transfer_queued);

		StringList xfer_states;
		if (transferring_input) xfer_states.append("in");
		if (transferring_output) xfer_states.append("out");
		if (transfer_queued) xfer_states.append("queued");
		if ( ! xfer_states.isEmpty()) {
			char * xfer = xfer_states.print_to_string();
			sprintf(misc, " transfer=%s", xfer);
			free(xfer);
		}
	}

	/* If the jobAd values are not set, OR the values are all zero,
		report no data collected. This can be true for a standard universe
		job that has not checkpointed yet. */

	if ( ! have_bytes) {
		//                                                                            12345678 12345678 12345678/s
		out += (univ==CONDOR_UNIVERSE_STANDARD) ? "   [ no i/o data collected ] " : "    .        .        .      ";
	} else {
		if(wall_clock < 1.0) wall_clock=1;

		/*
		Note: metric_units() cannot be used twice in the same
		statement -- it returns a pointer to a static buffer.
		*/

		if (read_bytes > 0) {
			formatstr_cat(out, " %8s", metric_units(read_bytes) );
		} else {
			out += "    .    ";
		}
		if (write_bytes > 0) {
			formatstr_cat(out, " %8s", metric_units(write_bytes) );
		} else {
			out += "    .    ";
		}
		if (read_bytes + write_bytes > 0) {
			formatstr_cat(out, " %8s/s", metric_units((int)((read_bytes+write_bytes)/wall_clock)) );
		} else {
			out += "    .      ";
		}
	}

	out += misc;
	out += "\n";
}


static char *
bufferJobShort( ClassAd *ad ) {
	int cluster, proc, date, status, prio, image_size, memory_usage;

	char encoded_status;
	int last_susp_time;

	double utime  = 0.0;
	char owner[64];
	char *cmd = NULL;
	MyString buffer;

	if (!ad->EvalInteger (ATTR_CLUSTER_ID, NULL, cluster)		||
		!ad->EvalInteger (ATTR_PROC_ID, NULL, proc)				||
		!ad->EvalInteger (ATTR_Q_DATE, NULL, date)				||
		!ad->EvalFloat   (ATTR_JOB_REMOTE_USER_CPU, NULL, utime)||
		!ad->EvalInteger (ATTR_JOB_STATUS, NULL, status)		||
		!ad->EvalInteger (ATTR_JOB_PRIO, NULL, prio)			||
		!ad->EvalInteger (ATTR_IMAGE_SIZE, NULL, image_size)	||
		!ad->EvalString  (ATTR_OWNER, NULL, owner)				||
		!ad->EvalString  (ATTR_JOB_CMD, NULL, &cmd) )
	{
		sprintf (return_buff, " --- ???? --- \n");
		return( return_buff );
	}
	
	// print memory usage unless it's unavailable, then print image size
	// note that memory usage is megabytes but imagesize is kilobytes.
	double memory_used_mb = image_size / 1024.0;
	if (ad->EvalInteger(ATTR_MEMORY_USAGE, NULL, memory_usage)) {
		memory_used_mb = memory_usage;
	}

	std::string description;
	if ( ! ad->EvalString("MATCH_EXP_" ATTR_JOB_DESCRIPTION, NULL, description)) {
		ad->EvalString(ATTR_JOB_DESCRIPTION, NULL, description);
	}
	if ( !description.empty() ){
		buffer.formatstr("%s", description.c_str());
	} else {
		buffer.formatstr( "%s", condor_basename(cmd) );
		MyString args_string;
		ArgList::GetArgsStringForDisplay(ad,&args_string);
		if ( ! args_string.IsEmpty()) {
			buffer.formatstr_cat( " %s", args_string.Value() );
		}
	}
	free(cmd);
	utime = job_time(utime,ad);

	encoded_status = encode_status( status );

	/* The suspension of a job is a second class citizen and is not a true
		status that can exist as a job status ad and is instead
		inferred, so therefore the processing and display of
		said suspension is also second class. */
	if (param_boolean("REAL_TIME_JOB_SUSPEND_UPDATES", false)) {
			if (!ad->EvalInteger(ATTR_LAST_SUSPENSION_TIME,NULL,last_susp_time))
			{
				last_susp_time = 0;
			}
			/* sanity check the last_susp_time against if the job is running
				or not in case the schedd hasn't synchronized the
				last suspension time attribute correctly to job running
				boundaries. */
			if ( status == RUNNING && last_susp_time != 0 )
			{
				encoded_status = 'S';
			}
	}

		// adjust status field to indicate file transfer status
	int transferring_input = false;
	int transferring_output = false;
	ad->EvalBool(ATTR_TRANSFERRING_INPUT,NULL,transferring_input);
	ad->EvalBool(ATTR_TRANSFERRING_OUTPUT,NULL,transferring_output);
	if( transferring_input ) {
		encoded_status = '<';
	}
	if( transferring_output ) {
		encoded_status = '>';
	}

	sprintf( return_buff,
			 widescreen ? description.empty() ? "%4d.%-3d %-14s %-11s %-12s %-2c %-3d %-4.1f %s\n"
			                                   : "%4d.%-3d %-14s %-11s %-12s %-2c %-3d %-4.1f (%s)\n"
			            : description.empty() ? "%4d.%-3d %-14s %-11s %-12s %-2c %-3d %-4.1f %.18s\n"
				                           : "%4d.%-3d %-14s %-11s %-12s %-2c %-3d %-4.1f (%.17s)\n",
			 cluster,
			 proc,
			 format_owner( owner, ad ),
			 format_date( (time_t)date ),
			 /* In the next line of code there is an (int) typecast. This
			 	has to be there otherwise the compiler produces incorrect code
				here and performs a structural typecast from the float to an
				int(like how a union works) and passes a garbage number to the
				format_time function. The compiler is *supposed* to do a
				functional typecast, meaning generate code that changes the
				float to an int legally. Apparently, that isn't happening here.
				-psilord 09/06/01 */
			 format_time( (int)utime ),
			 encoded_status,
			 prio,
			 memory_used_mb,
			 buffer.Value() );

	return return_buff;
}

static void 
short_header (void)
{
	printf( " %-7s %-14s ", "ID", dash_dag ? "OWNER/NODENAME" : "OWNER" );
	if (dash_goodput) {
		printf( "%11s %12s %-16s\n", "SUBMITTED", JOB_TIME,
				"GOODPUT CPU_UTIL   Mb/s" );
	} else if (dash_globus) {
		printf( "%-10s %-8s %-18s  %-18s\n", 
				"STATUS", "MANAGER", "HOST", "EXECUTABLE" );
	} else if ( dash_grid ) {
		printf( "%-10s  %-16s %-16s  %-18s\n", 
				"STATUS", "GRID->MANAGER", "HOST", "GRID-JOB-ID" );
	} else if ( show_held ) {
		printf( "%11s %-30s\n", "HELD_SINCE", "HOLD_REASON" );
	} else if ( show_io ) {
		printf( "%4s %2s %-8s %-8s %-10s %s\n",
				"RUNS", "ST", " INPUT", " OUTPUT", " RATE", "MISC");
	} else if( dash_run ) {
		printf( "%11s %12s %-16s\n", "SUBMITTED", JOB_TIME, "HOST(S)" );
	} else {
		printf( "%11s %12s %-2s %-3s %-4s %-18s\n",
			"SUBMITTED",
			JOB_TIME,
			"ST",
			"PRI",
			"SIZE",
			"CMD"
		);
	}
}

static const char *
format_remote_host (const char *, AttrList *ad, Formatter &)
{
	static char host_result[MAXHOSTNAMELEN];
	static char unknownHost [] = "[????????????????]";
	condor_sockaddr addr;

	int universe = CONDOR_UNIVERSE_STANDARD;
	ad->LookupInteger( ATTR_JOB_UNIVERSE, universe );
	if (((universe == CONDOR_UNIVERSE_SCHEDULER) || (universe == CONDOR_UNIVERSE_LOCAL)) &&
		addr.from_sinful(scheddAddr) == true) {
		MyString hostname = get_hostname(addr);
		if (hostname.Length() > 0) {
			strcpy( host_result, hostname.Value() );
			return host_result;
		} else {
			return unknownHost;
		}
	} else if (universe == CONDOR_UNIVERSE_GRID) {
		if (ad->LookupString(ATTR_EC2_REMOTE_VM_NAME,host_result,sizeof(host_result)) == 1)
			return host_result;
		else if (ad->LookupString(ATTR_GRID_RESOURCE,host_result, sizeof(host_result)) == 1 )
			return host_result;
		else
			return unknownHost;
	}

	if (ad->LookupString(ATTR_REMOTE_HOST, host_result, sizeof(host_result)) == 1) {
		if( is_valid_sinful(host_result) && 
			addr.from_sinful(host_result) == true ) {
			MyString hostname = get_hostname(addr);
			if (hostname.Length() > 0) {
				strcpy(host_result, hostname.Value());
			} else {
				return unknownHost;
			}
		}
		return host_result;
	}
	return unknownHost;
}

static const char *
format_cpu_time (double utime, AttrList *ad, Formatter &)
{
	return format_time( (int) job_time(utime,(ClassAd *)ad) );
}

static const char *
format_memory_usage(int image_size, AttrList *ad, Formatter &)
{
	static char put_result[10];
	long long memory_usage;
	// print memory usage unless it's unavailable, then print image size
	// note that memory usage is megabytes but imagesize is kilobytes.
	double memory_used_mb = image_size / 1024.0;
	if (ad->EvalInteger(ATTR_MEMORY_USAGE, NULL, memory_usage)) {
		memory_used_mb = memory_usage;
	}
	sprintf(put_result, "%-4.1f", memory_used_mb);
	return put_result;
}

static const char *
format_readable_mb(const classad::Value &val, AttrList *, Formatter &)
{
	long long kbi;
	double kb;
	if (val.IsIntegerValue(kbi)) {
		kb = kbi * 1024.0 * 1024.0;
	} else if (val.IsRealValue(kb)) {
		kb *= 1024.0 * 1024.0;
	} else {
		return "        ";
	}
	return metric_units(kb);
}

static const char *
format_readable_kb(const classad::Value &val, AttrList *, Formatter &)
{
	long long kbi;
	double kb;
	if (val.IsIntegerValue(kbi)) {
		kb = kbi*1024.0;
	} else if (val.IsRealValue(kb)) {
		kb *= 1024.0;
	} else {
		return "        ";
	}
	return metric_units(kb);
}

static const char *
format_readable_bytes(const classad::Value &val, AttrList *, Formatter &)
{
	long long kbi;
	double kb;
	if (val.IsIntegerValue(kbi)) {
		kb = kbi;
	} else if (val.IsRealValue(kb)) {
		// nothing to do.
	} else {
		return "        ";
	}
	return metric_units(kb);
}

static const char *
format_job_description(const char* cmd, AttrList *ad, Formatter &)
{
	static MyString put_result;
	std::string description;
	if ( ! ad->EvalString("MATCH_EXP_" ATTR_JOB_DESCRIPTION, NULL, description)) {
		ad->EvalString(ATTR_JOB_DESCRIPTION, NULL, description);
	}
	if ( ! description.empty()) {
		put_result.formatstr("(%s)", description.c_str());
	} else {
		put_result = condor_basename(cmd);
		MyString args_string;
		ArgList::GetArgsStringForDisplay(ad,&args_string);
		if ( ! args_string.IsEmpty()) {
			put_result.formatstr_cat( " %s", args_string.Value() );
			//put_result += " ";
			//put_result += args_string.Value();
		}
	}
	return put_result.c_str();
}

static const char *
format_job_universe(int job_universe, AttrList* /*ad*/, Formatter &)
{
	return CondorUniverseNameUcFirst(job_universe);
}

static const char *
format_job_id(int cluster_id, AttrList* ad, Formatter &)
{
	static char put_result[PROC_ID_STR_BUFLEN];
	int proc_id=0;
	ad->LookupInteger(ATTR_PROC_ID,proc_id);
	ProcIdToStr(cluster_id, proc_id, put_result);
	return put_result;
}

static const char *
format_job_status_raw(int job_status, AttrList* /*ad*/, Formatter &)
{
	switch(job_status) {
	case IDLE:      return "Idle   ";
	case HELD:      return "Held   ";
	case RUNNING:   return "Running";
	case COMPLETED: return "Complet";
	case REMOVED:   return "Removed";
	case SUSPENDED: return "Suspend";
	case TRANSFERRING_OUTPUT: return "XFerOut";
	default:        return "Unk    ";
	}
}

static const char *
format_job_status_char(int job_status, AttrList*ad, Formatter &)
{
	static char put_result[3];
	put_result[1] = ' ';
	put_result[2] = 0;

	put_result[0] = encode_status(job_status);

	/* The suspension of a job is a second class citizen and is not a true
		status that can exist as a job status ad and is instead
		inferred, so therefore the processing and display of
		said suspension is also second class. */
	if (param_boolean("REAL_TIME_JOB_SUSPEND_UPDATES", false)) {
		int last_susp_time;
		if (!ad->EvalInteger(ATTR_LAST_SUSPENSION_TIME,NULL,last_susp_time))
		{
			last_susp_time = 0;
		}
		/* sanity check the last_susp_time against if the job is running
			or not in case the schedd hasn't synchronized the
			last suspension time attribute correctly to job running
			boundaries. */
		if ( job_status == RUNNING && last_susp_time != 0 )
		{
			put_result[0] = 'S';
		}
	}

		// adjust status field to indicate file transfer status
	int transferring_input = false;
	int transferring_output = false;
	int transfer_queued = false;
	ad->EvalBool(ATTR_TRANSFERRING_INPUT,NULL,transferring_input);
	ad->EvalBool(ATTR_TRANSFERRING_OUTPUT,NULL,transferring_output);
	ad->EvalBool(ATTR_TRANSFER_QUEUED,NULL,transfer_queued);
	if( transferring_input ) {
		put_result[0] = '<';
		put_result[1] = transfer_queued ? 'q' : ' ';
	}
	if( transferring_output || job_status == TRANSFERRING_OUTPUT) {
		put_result[0] = transfer_queued ? 'q' : ' ';
		put_result[1] = '>';
	}
	return put_result;
}

static const char *
format_goodput (int job_status, AttrList *ad, Formatter & /*fmt*/)
{
	static char put_result[9];
	int ckpt_time = 0, shadow_bday = 0, last_ckpt = 0;
	float wall_clock = 0.0;
	ad->LookupInteger( ATTR_JOB_COMMITTED_TIME, ckpt_time );
	ad->LookupInteger( ATTR_SHADOW_BIRTHDATE, shadow_bday );
	ad->LookupInteger( ATTR_LAST_CKPT_TIME, last_ckpt );
	ad->LookupFloat( ATTR_JOB_REMOTE_WALL_CLOCK, wall_clock );
	if ((job_status == RUNNING || job_status == TRANSFERRING_OUTPUT || job_status == SUSPENDED) &&
		shadow_bday && last_ckpt > shadow_bday)
	{
		wall_clock += last_ckpt - shadow_bday;
	}
	if (wall_clock <= 0.0) return " [?????]";
	float goodput_time = ckpt_time/wall_clock*100.0;
	if (goodput_time > 100.0) goodput_time = 100.0;
	else if (goodput_time < 0.0) return " [?????]";
	sprintf(put_result, " %6.1f%%", goodput_time);
	return put_result;
}

static const char *
format_mbps (double bytes_sent, AttrList *ad, Formatter & /*fmt*/)
{
	static char result_format[10];
	double wall_clock=0.0, bytes_recvd=0.0, total_mbits;
	int shadow_bday = 0, last_ckpt = 0, job_status = IDLE;
	ad->LookupFloat( ATTR_JOB_REMOTE_WALL_CLOCK, wall_clock );
	ad->LookupInteger( ATTR_SHADOW_BIRTHDATE, shadow_bday );
	ad->LookupInteger( ATTR_LAST_CKPT_TIME, last_ckpt );
	ad->LookupInteger( ATTR_JOB_STATUS, job_status );
	if ((job_status == RUNNING || job_status == TRANSFERRING_OUTPUT || job_status == SUSPENDED) && shadow_bday && last_ckpt > shadow_bday) {
		wall_clock += last_ckpt - shadow_bday;
	}
	ad->LookupFloat(ATTR_BYTES_RECVD, bytes_recvd);
	total_mbits = (bytes_sent+bytes_recvd)*8/(1024*1024); // bytes to mbits
	if (total_mbits <= 0) return " [????]";
	sprintf(result_format, " %6.2f", total_mbits/wall_clock);
	return result_format;
}

static const char *
format_cpu_util (double utime, AttrList *ad, Formatter & /*fmt*/)
{
	static char result_format[10];
	int ckpt_time = 0;
	ad->LookupInteger( ATTR_JOB_COMMITTED_TIME, ckpt_time);
	if (ckpt_time == 0) return " [??????]";
	double util = utime/ckpt_time*100.0;
	if (util > 100.0) util = 100.0;
	else if (util < 0.0) return " [??????]";
	sprintf(result_format, "  %6.1f%%", util);
	return result_format;
}

static const char *
format_owner_common (const char *owner, AttrList *ad)
{
	static char result_str[100] = "";

	// [this is a somewhat kludgey place to implement DAG formatting,
	// but for a variety of reasons (maintainability, allowing future
	// changes to be made in only one place, etc.), Todd & I decided
	// it's the best way to do it given the existing code...  --pfc]

	// if -dag is specified, check whether this job was started by a
	// DAGMan (by seeing if it has a valid DAGManJobId attribute), and
	// if so, print DAGNodeName in place of owner

	// (we need to check that the DAGManJobId is valid because DAGMan
	// >= v6.3 inserts "unknown..." into DAGManJobId when run under a
	// pre-v6.3 schedd)

	if ( dash_dag && ad->LookupExpr( ATTR_DAGMAN_JOB_ID ) ) {
			// We have a DAGMan job ID, this means we have a DAG node
			// -- don't worry about what type the DAGMan job ID is.
		if ( ad->LookupString( ATTR_DAG_NODE_NAME, result_str, COUNTOF(result_str) ) ) {
			return result_str;
		} else {
			fprintf(stderr, "DAG node job with no %s attribute!\n",
					ATTR_DAG_NODE_NAME);
		}
	}

	int niceUser;
	if (ad->LookupInteger( ATTR_NICE_USER, niceUser) && niceUser ) {
		sprintf(result_str, "%s.%s", NiceUserName, owner);
	} else {
		strcpy_len(result_str, owner, COUNTOF(result_str));
	}
	return result_str;
}

static const char *
format_owner (const char *owner, AttrList *ad)
{
	static char result_format[24];
	sprintf(result_format, "%-14.14s", format_owner_common(owner, ad));
	return result_format;
}

static const char *
format_owner_wide (const char *owner, AttrList *ad, Formatter & /*fmt*/)
{
	return format_owner_common(owner, ad);
}

static const char *
format_globusStatus( int globusStatus, AttrList * /* ad */, Formatter & /*fmt*/ )
{
	static char result_str[64];
#if defined(HAVE_EXT_GLOBUS)
	sprintf(result_str, " %7.7s", GlobusJobStatusName( globusStatus ) );
#else
	static const struct {
		int status;
		const char * psz;
	} gram_states[] = {
		{ 1, "PENDING" },	 //GLOBUS_GRAM_PROTOCOL_JOB_STATE_PENDING = 1,
		{ 2, "ACTIVE" },	 //GLOBUS_GRAM_PROTOCOL_JOB_STATE_ACTIVE = 2,
		{ 4, "FAILED" },	 //GLOBUS_GRAM_PROTOCOL_JOB_STATE_FAILED = 4,
		{ 8, "DONE" },	 //GLOBUS_GRAM_PROTOCOL_JOB_STATE_DONE = 8,
		{16, "SUSPEND" },	 //GLOBUS_GRAM_PROTOCOL_JOB_STATE_SUSPENDED = 16,
		{32, "UNSUBMIT" },	 //GLOBUS_GRAM_PROTOCOL_JOB_STATE_UNSUBMITTED = 32,
		{64, "STAGE_IN" },	 //GLOBUS_GRAM_PROTOCOL_JOB_STATE_STAGE_IN = 64,
		{128,"STAGE_OUT" },	 //GLOBUS_GRAM_PROTOCOL_JOB_STATE_STAGE_OUT = 128,
	};
	for (size_t ii = 0; ii < COUNTOF(gram_states); ++ii) {
		if (globusStatus == gram_states[ii].status) {
			return gram_states[ii].psz;
		}
	}
	sprintf(result_str, "%d", globusStatus);
#endif
	return result_str;
}

// The remote hostname may be in GlobusResource or GridResource.
// We want this function to be called if at least one is defined,
// but it will only be called if the one attribute it's registered
// with is defined. So we register it with an attribute we know will
// always be present and be a string. We then ignore that attribute
// and examine GlobusResource and GridResource.
static const char *
format_globusHostAndJM(const char *, AttrList *ad, Formatter & /*fmt*/ )
{
	static char result_format[64];
	char	host[80] = "[?????]";
	char	jm[80] = "fork";
	char	*tmp;
	int	p;
	char *attr_value = NULL;
	char *resource_name = NULL;
	char *grid_type = NULL;

	if ( ad->LookupString( ATTR_GRID_RESOURCE, &attr_value ) ) {
			// If ATTR_GRID_RESOURCE exists, skip past the initial
			// '<job type> '.
		resource_name = strchr( attr_value, ' ' );
		if ( resource_name ) {
			*resource_name = '\0';
			grid_type = strdup( attr_value );
			resource_name++;
		}
	}

	if ( resource_name != NULL ) {

		if ( grid_type == NULL || !strcasecmp( grid_type, "gt2" ) ||
			 !strcasecmp( grid_type, "gt5" ) ||
			 !strcasecmp( grid_type, "globus" ) ) {

			// copy the hostname
			p = strcspn( resource_name, ":/" );
			if ( p >= (int) sizeof(host) )
				p = sizeof(host) - 1;
			strncpy( host, resource_name, p );
			host[p] = '\0';

			if ( ( tmp = strstr( resource_name, "jobmanager-" ) ) != NULL ) {
				tmp += 11; // 11==strlen("jobmanager-")

				// copy the jobmanager name
				p = strcspn( tmp, ":" );
				if ( p >= (int) sizeof(jm) )
					p = sizeof(jm) - 1;
				strncpy( jm, tmp, p );
				jm[p] = '\0';
			}

		} else if ( !strcasecmp( grid_type, "gt4" ) ) {

			strcpy( jm, "Fork" );

				// GridResource is of the form '<service url> <jm type>'
				// Find the space, zero it out, and grab the jm type from
				// the end (if it's non-empty).
			tmp = strchr( resource_name, ' ' );
			if ( tmp ) {
				*tmp = '\0';
				if ( tmp[1] != '\0' ) {
					strcpy( jm, &tmp[1] );
				}
			}

				// Pick the hostname out of the URL
			if ( strncmp( "https://", resource_name, 8 ) == 0 ) {
				strncpy( host, &resource_name[8], sizeof(host) );
				host[sizeof(host)-1] = '\0';
			} else {
				strncpy( host, resource_name, sizeof(host) );
				host[sizeof(host)-1] = '\0';
			}
			p = strcspn( host, ":/" );
			host[p] = '\0';
		}
	}

	if ( grid_type ) {
		free( grid_type );
	}

	if ( attr_value ) {
		free( attr_value );
	}

	// done --- pack components into the result string and return
	sprintf( result_format, " %-8.8s %-18.18s  ", jm, host );
	return( result_format );
}

static const char *
format_gridStatus( int jobStatus, AttrList * ad, Formatter & fmt )
{
	static char result_str[32];
	if (ad->LookupString( ATTR_GRID_JOB_STATUS, result_str, COUNTOF(result_str) )) {
		return result_str;
	} 
	int globusStatus = 0;
	if (ad->LookupInteger( ATTR_GLOBUS_STATUS, globusStatus )) {
		return format_globusStatus(globusStatus, ad, fmt);
	} 
	static const struct {
		int status;
		const char * psz;
	} states[] = {
		{ IDLE, "IDLE" },
		{ RUNNING, "RUNNING" },
		{ COMPLETED, "COMPLETED" },
		{ HELD, "HELD" },
		{ SUSPENDED, "SUSPENDED" },
		{ REMOVED, "REMOVED" },
		{ TRANSFERRING_OUTPUT, "XFER_OUT" },
	};
	for (size_t ii = 0; ii < COUNTOF(states); ++ii) {
		if (jobStatus == states[ii].status) {
			return states[ii].psz;
		}
	}
	sprintf(result_str, "%d", jobStatus);
	return result_str;
}

#if 0 // not currently used, disabled to shut up fedora.
static const char *
format_gridType( int , AttrList * ad )
{
	static char result[64];
	char grid_res[64];
	if (ad->LookupString( ATTR_GRID_RESOURCE, grid_res, COUNTOF(grid_res) )) {
		char * p = result;
		char * r = grid_res;
		*p++ = ' ';
		while (*r && *r != ' ') {
			*p++ = *r++;
		}
		while (p < &result[5]) *p++ = ' ';
		*p++ = 0;
	} else {
		strcpy_len(result, "   ?  ", sizeof(result));
	}
	return result;
}
#endif

static const char *
format_gridResource(const char * grid_res, AttrList * ad, Formatter & /*fmt*/ )
{
	std::string grid_type;
	std::string str = grid_res;
	std::string mgr = "[?]";
	std::string host = "[???]";
	const bool fshow_host_port = false;
	const unsigned int width = 1+6+1+8+1+18+1;

	// GridResource is a string with the format 
	//      "type host_url manager" (where manager can contain whitespace)
	// or   "type host_url/jobmanager-manager"
	unsigned int ixHost = str.find_first_of(' ');
	if (ixHost < str.length()) {
		grid_type = str.substr(0, ixHost);
		ixHost += 1; // skip over space.
	} else {
		grid_type = "globus";
		ixHost = 0;
	}

	//if ((MATCH == grid_type.find("gt")) || (MATCH == grid_type.compare("globus"))){
	//	return format_globusHostAndJM( grid_res, ad );
	//}

	unsigned int ix2 = str.find_first_of(' ', ixHost);
	if (ix2 < str.length()) {
		mgr = str.substr(ix2+1);
	} else {
		unsigned int ixMgr = str.find("jobmanager-", ixHost);
		if (ixMgr < str.length()) 
			mgr = str.substr(ixMgr+11);	//sizeof("jobmanager-") == 11
		ix2 = ixMgr;
	}

	unsigned int ix3 = str.find("://", ixHost);
	ix3 = (ix3 < str.length()) ? ix3+3 : ixHost;
	unsigned int ix4 = str.find_first_of(fshow_host_port ? "/" : ":/",ix3);
	if (ix4 > ix2) ix4 = ix2;
	host = str.substr(ix3, ix4-ix3);

	MyString mystr = mgr.c_str();
	mystr.replaceString(" ", "/");
	mgr = mystr.Value();

    static char result_str[1024];
    if( MATCH == grid_type.compare( "ec2" ) ) {
        // mgr = str.substr( ixHost, ix3 - ixHost - 3 );
        char rvm[MAXHOSTNAMELEN];
        if( ad->LookupString( ATTR_EC2_REMOTE_VM_NAME, rvm, sizeof( rvm ) ) ) {
            host = rvm;
        }
        
        snprintf( result_str, 1024, "%s %s", grid_type.c_str(), host.c_str() );
    } else {
    	snprintf(result_str, 1024, "%s->%s %s", grid_type.c_str(), mgr.c_str(), host.c_str());
    }
	result_str[COUNTOF(result_str)-1] = 0;

	ix2 = strlen(result_str);
	if ( ! widescreen) ix2 = width;
	result_str[ix2] = 0;
	return result_str;
}

static const char *
format_gridJobId(const char * grid_jobid, AttrList *ad, Formatter & /*fmt*/ )
{
	std::string str = grid_jobid;
	std::string jid;
	std::string host;

	std::string grid_type = "globus";
	char grid_res[64];
	if (ad->LookupString( ATTR_GRID_RESOURCE, grid_res, COUNTOF(grid_res) )) {
		char * r = grid_res;
		while (*r && *r != ' ') {
			++r;
		}
		*r = 0;
		grid_type = grid_res;
	}
	bool gram = (MATCH == grid_type.compare("gt5")) || (MATCH == grid_type.compare("gt2"));

	unsigned int ix2 = str.find_last_of(" ");
	ix2 = (ix2 < str.length()) ? ix2 + 1 : 0;

	unsigned int ix3 = str.find("://", ix2);
	ix3 = (ix3 < str.length()) ? ix3+3 : ix2;
	unsigned int ix4 = str.find_first_of("/",ix3);
	ix4 = (ix4 < str.length()) ? ix4 : ix3;
	host = str.substr(ix3, ix4-ix3);

	if (gram) {
		jid += host;
		jid += " : ";
		if (str[ix4] == '/') ix4 += 1;
		unsigned int ix5 = str.find_first_of("/",ix4);
		jid = str.substr(ix4, ix5-ix4);
		if (ix5 < str.length()) {
			if (str[ix5] == '/') ix5 += 1;
			unsigned int ix6 = str.find_first_of("/",ix5);
			jid += ".";
			jid += str.substr(ix5, ix6-ix5);
		}
	} else {
		//jid = grid_type;
		//jid += " : ";
		jid += str.substr(ix4);
	}

	static char result_str[1024];
	sprintf(result_str, "%s", jid.c_str());
	return result_str;
}

static const char *
format_q_date (int d, AttrList *, Formatter &)
{
	return format_date(d);
}


static void
usage (const char *myName, int other)
{
	printf ("Usage: %s [general-opts] [restriction-list] [output-opts | analyze-opts]\n", myName);
	printf ("\n    [general-opts] are\n"
		"\t-global\t\t\tQuery all Schedulers in this pool\n"
		"\t-schedd-constraint\tQuery all Schedulers matching this constraint\n"
		"\t-submitter <submitter>\tGet queue of specific submitter\n"
		"\t-name <name>\t\tName of Scheduler\n"
		"\t-pool <host>\t\tUse host as the central manager to query\n"
#ifdef HAVE_EXT_POSTGRESQL
		"\t-direct <rdbms | schedd>\n"
		"\t\tPerform a direct query to the rdbms\n"
		"\t\tor to the schedd without falling back to the queue\n"
		"\t\tlocation discovery algortihm, even in case of error\n"
		"\t-avgqueuetime\t\tAverage queue time for uncompleted jobs\n"
#endif
		"\t-jobads <file>\t\tRead queue from a file of job ClassAds\n"
		"\t-userlog <file>\t\tRead queue from a user log file\n"
		);

	printf ("\n    [restriction-list] each restriction may be one of\n"
		"\t<cluster>\t\tGet information about specific cluster\n"
		"\t<cluster>.<proc>\tGet information about specific job\n"
		"\t<owner>\t\t\tInformation about jobs owned by <owner>\n"
		"\t-autocluster\t\tGet information about the SCHEDD's autoclusters\n"
		"\t-constraint <expr>\tGet information about jobs that match <expr>\n"
		);

	printf ("\n    [output-opts] are\n"
		"\t-limit <num>\t\tLimit the number of results to <num>\n"
		"\t-cputime\t\tDisplay CPU_TIME instead of RUN_TIME\n"
		"\t-currentrun\t\tDisplay times only for current run\n"
		"\t-debug\t\t\tDisplay debugging info to console\n"
		"\t-dag\t\t\tSort DAG jobs under their DAGMan\n"
		"\t-expert\t\t\tDisplay shorter error messages\n"
		"\t-globus\t\t\tGet information about jobs of type globus\n"
		"\t-goodput\t\tDisplay job goodput statistics\n"	
		"\t-help [Universe|State]\tDisplay this screen, JobUniverses, JobStates\n"
		"\t-hold\t\t\tGet information about jobs on hold\n"
		"\t-io\t\t\tDisplay information regarding I/O\n"
//FUTURE		"\t-transfer\t\tDisplay information for jobs that are doing file transfer\n"
		"\t-run\t\t\tGet information about running jobs\n"
		"\t-totals\t\t\tDisplay only job totals\n"
		"\t-stream-results \tProduce output as jobs are fetched\n"
		"\t-version\t\tPrint the HTCondor version and exit\n"
		"\t-wide[:<width>]\t\tDon't truncate data to fit in 80 columns.\n"
		"\t\t\t\tTruncates to console width or <width> argument.\n"
		"\t-autoformat[:jlhVr,tng] <attr> [<attr2> [...]]\n"
		"\t-af[:jlhVr,tng] <attr> [attr2 [...]]\n"
		"\t    Print attr(s) with automatic formatting\n"
		"\t    the [jlhVr,tng] options modify the formatting\n"
		"\t        j   Display Job id\n"
		"\t        l   attribute labels\n"
		"\t        h   attribute column headings\n"
		"\t        V   %%V formatting (string values are quoted)\n"
		"\t        r   %%r formatting (raw/unparsed values)\n"
		"\t        t   tab before each value (default is space)\n"
		"\t        g   newline between ClassAds, no space before values\n"
		"\t        ,   comma after each value\n"
		"\t        n   newline after each value\n"
		"\t    use -af:h to get tabular values with headings\n"
		"\t    use -af:lrng to get -long equivalant format\n"
		"\t-format <fmt> <attr>\tPrint attribute attr using format fmt\n"
		"\t-print-format <file>\tUse <file> to set display attributes and formatting\n"
		"\t\t\t\t(experimental, see htcondor-wiki for more information)\n"
		"\t-long\t\t\tDisplay entire ClassAds\n"
		"\t-xml\t\t\tDisplay entire ClassAds, but in XML\n"
		"\t-attributes X,Y,...\tAttributes to show in -xml and -long\n"
		);

	printf ("\n    [analyze-opts] are\n"
		"\t-analyze[:<qual>]\tPerform matchmaking analysis on jobs\n"
		"\t-better-analyze[:<qual>]\tPerform more detailed match analysis\n"
		"\t    <qual> is a comma separated list of one or more of\n"
		"\t    priority\tConsider user priority during analysis\n"
		"\t    summary\tShow a one-line summary for each job or machine\n"
		"\t    reverse\tAnalyze machines rather than jobs\n"
		"\t-machine <name>\t\tMachine name or slot name for analysis\n"
		"\t-mconstraint <expr>\tMachine constraint for analysis\n"
		"\t-slotads <file>\t\tRead Machine ClassAds for analysis from <file>\n"
		"\t\t\t\t<file> can be the output of condor_status -long\n"
		"\t-userprios <file>\tRead user priorities for analysis from <file>\n"
		"\t\t\t\t<file> can be the output of condor_userprio -l\n"
		"\t-nouserprios\t\tDon't consider user priority during analysis\n"
		"\t-reverse\t\tAnalyze Machine requirements against jobs\n"
		"\t-verbose\t\tShow progress and machine names in results\n"
		"\n"
		);

	if (other & usage_Universe) {
		printf("    %s codes:\n", ATTR_JOB_UNIVERSE);
		for (int uni = CONDOR_UNIVERSE_MIN+1; uni < CONDOR_UNIVERSE_MAX; ++uni) {
			if (uni == CONDOR_UNIVERSE_PIPE) { // from PIPE -> Linda is obsolete.
				uni = CONDOR_UNIVERSE_LINDA;
				continue;
			}
			printf("\t%2d %s\n", uni, CondorUniverseNameUcFirst(uni));
		}
		printf("\n");
	}
	if (other & usage_JobStatus) {
		printf("    %s codes:\n", ATTR_JOB_STATUS);
		for (int st = JOB_STATUS_MIN; st <= JOB_STATUS_MAX; ++st) {
			printf("\t%2d %c %s\n", st, encode_status(st), getJobStatusString(st));
		}
		printf("\n");
	}
}

static void
print_full_footer()
{
	// If we want to summarize, do that too.
	if( summarize ) {
		printf( "\n%d jobs; "
				"%d completed, %d removed, %d idle, %d running, %d held, %d suspended",
				idle+running+held+malformed+suspended+completed+removed,
				completed,removed,idle,running,held,suspended);
		if (malformed>0) printf( ", %d malformed",malformed);
		printf("\n");
	}
	if( use_xml ) {
			// keep this consistent with AttrListList::fPrintAttrListList()
		std::string xml;
		AddClassAdXMLFileFooter(xml);
		printf("%s\n", xml.c_str());
	}
}

static void
print_full_header(const char * source_label)
{
	if ( ! dash_long) {
		// print the source label.
		if ( ! (customHeadFoot&HF_NOTITLE)) {
			static bool first_time = false;
			printf ("%s-- %s\n", (first_time ? "" : "\n\n"), source_label);
			first_time = false;
		}
		if ( ! (customHeadFoot&HF_NOHEADER)) {
			// Print the output header
			if (usingPrintMask && mask.has_headings()) {
				mask.display_Headings(stdout);
			} else if ( ! better_analyze) {
				short_header();
			}
		}
	}
	if( use_xml ) {
			// keep this consistent with AttrListList::fPrintAttrListList()
		std::string xml;
		AddClassAdXMLFileHeader(xml);
		printf("%s\n", xml.c_str());
	}
}

#ifdef ALLOW_DASH_DAG

static void
edit_string(clusterProcString *cps)
{
	if(!cps->parent) {
		return;	// Nothing to do
	}
	std::string s;
	int generations = 0;
	for( clusterProcString* ccps = cps->parent; ccps; ccps = ccps->parent ) {
		++generations;
	}
	int state = 0;
	for(const char* p = cps->string; *p;){
		switch(state) {
		case 0:
			if(!isspace(*p)){
				state = 1;
			} else {
				s += *p;
				++p;
			}
			break;
		case 1:
			if(isspace(*p)){
				state = 2;
			} else {
				s += *p;
				++p;
			}
			break;
		case 2: if(isspace(*p)){
				s+=*p;
				++p;
			} else {
				for(int i=0;i<generations;++i){
					s+=' ';
				}
				s +="|-";
				state = 3;
			}
			break;
		case 3:
			if(isspace(*p)){
				state = 4;
			} else {
				s += *p;
				++p;	
			}
			break;
		case 4:
			int gen_i;
			for(gen_i=0;gen_i<=generations+1;++gen_i){
				if(isspace(*p)){
					++p;
				} else {
					break;
				}
			}
			if( gen_i < generations || !isspace(*p) ) {
				std::string::iterator sp = s.end();
				--sp;
				*sp = ' ';
			}
			state = 5;
			break;
		case 5:
			s += *p;
			++p;
			break;
		}
	}
	char* cpss = cps->string;
	cps->string = strnewp( s.c_str() );
	delete[] cpss;
}

static void
rewrite_output_for_dag_nodes_in_dag_map()
{
	for(dag_map_type::iterator cps = dag_map.begin(); cps != dag_map.end(); ++cps) {
		edit_string(cps->second);
	}
}

// First Pass: Find all the DAGman nodes, and link them up
// Assumptions of this code: DAG Parent nodes always
// have cluster ids that are less than those of child
// nodes. condor_dagman processes have ProcID == 0 (only one per
// cluster)
static void
linkup_dag_nodes_in_dag_map()
{
	for(dag_map_type::iterator cps = dag_map.begin(); cps != dag_map.end(); ++cps) {
		clusterProcString* cpps = cps->second;
		if(cpps->dagman_cluster_id != cpps->cluster) {
				// Its probably a DAGman job
				// This next line is where we assume all condor_dagman
				// jobs have ProcID == 0
			clusterProcMapper parent_id(cpps->dagman_cluster_id);
			dag_map_type::iterator dmp = dag_map.find(parent_id);
			if( dmp != dag_map.end() ) { // found it!
				cpps->parent = dmp->second;
				dmp->second->children.push_back(cpps);
			} else { // Now search dag_cluster_map
					// Necessary to find children of dags
					// which are themselves children (that is,
					// subdags)
				clusterIDProcIDMapper cipim(cpps->dagman_cluster_id);
				dag_cluster_map_type::iterator dcmti = dag_cluster_map.find(cipim);
				if(dcmti != dag_cluster_map.end() ) {
					cpps->parent = dcmti->second;
					dcmti->second->children.push_back(cpps);
				}
			}
		}
	}
}

static void print_dag_map_node(clusterProcString* cps,int level)
{
	if(cps->parent && level <= 0) {
		return;
	}
	printf("%s",cps->string);
	for(std::vector<clusterProcString*>::iterator p = cps->children.begin();
			p != cps->children.end(); ++p) {
		print_dag_map_node(*p,level+1);
	}
}

static void print_dag_map()
{
	for(dag_map_type::iterator cps = dag_map.begin(); cps != dag_map.end(); ++cps) {
		print_dag_map_node(cps->second,0);
	}
}

static void clear_dag_map()
{
	for(std::map<clusterProcMapper,clusterProcString*,CompareProcMaps>::iterator
			cps = dag_map.begin(); cps != dag_map.end(); ++cps) {
		delete[] cps->second->string;
		delete cps->second;
	}
	dag_map.clear();
	dag_cluster_map.clear();
}
#endif // ALLOW_DASH_DAG

/* print-format definition for default "bufferJobShort" output
SELECT
   ClusterId     AS " ID"  NOSUFFIX WIDTH 4
   ProcId        AS " "    NOPREFIX             PRINTF ".%-3d"
   Owner         AS "OWNER"         WIDTH -14   PRINTAS OWNER
   QDate         AS "  SUBMITTED"   WIDTH 11    PRINTAS QDATE
   RemoteUserCpu AS "    RUN_TIME"  WIDTH 12    PRINTAS CPU_TIME
   JobStatus     AS ST                          PRINTAS JOB_STATUS
   JobPrio       AS PRI
   ImageSize     AS SIZE            WIDTH 4     PRINTAS MEMORY_USAGE
   Cmd           AS CMD             WIDTH -18   PRINTAS JOB_DESCRIPTION
SUMMARY STANDARD
*/

static bool init_user_override_output_mask()
{
	// if someone already setup a print mask, then don't use an override file.
	if (usingPrintMask || disable_user_print_files)
		return false;

	bool using_override = false;
	MyString param_name("Q_DEFAULT_");
	const char * pdash = "";
	if (dash_run) { pdash = "RUN"; }
	else if (dash_goodput) { pdash = "GOODPUT"; }
	else if (dash_globus) { pdash = "GLOBUS"; }
	else if (dash_grid) { pdash = "GRID"; }
	else if (show_held) { pdash = "HOLD"; }
	if (pdash[0]) {
		param_name += pdash;
		param_name += "_";
	}
	param_name += "PRINT_FORMAT_FILE";
	char * pf_file = param(param_name.c_str());
	if (pf_file) {
		struct stat stat_buff;
		if (0 != stat(pf_file, &stat_buff)) {
			// do nothing, this is not an error.
		} else if (set_print_mask_from_stream(pf_file, true, attrs) < 0) {
			fprintf(stderr, "Warning: default %s print-format file '%s' is invalid\n", pdash, pf_file);
		} else {
			using_override = true;
			if (customHeadFoot&HF_NOSUMMARY) summarize = false;
		}
		free(pf_file);
	}
	return using_override;
}

static void init_output_mask()
{
	// just  in case we re-enter this function, only setup the mask once
	static bool	setup_mask = false;
	// TJ: before my 2012 refactoring setup_mask didn't protect summarize, so I'm preserving that.
	if ( dash_run || dash_goodput || dash_globus || dash_grid ) 
		summarize = false;
	else if ((customHeadFoot&HF_NOSUMMARY) && ! show_held)
		summarize = false;

	if (setup_mask)
		return;
	setup_mask = true;

	// if there is a user-override output mask, then use that instead of the code below
	if (init_user_override_output_mask()) {
		return;
	}

	int console_width = getDisplayWidth();

	const char * mask_headings = NULL;

	if (dash_run || dash_goodput) {
		//mask.SetAutoSep("<","{","},",">\n"); // for testing.
		mask.SetAutoSep(NULL," ",NULL,"\n");
		mask.registerFormat ("%4d.", 5, FormatOptionAutoWidth | FormatOptionNoSuffix, ATTR_CLUSTER_ID);
		mask.registerFormat ("%-3d", 3, FormatOptionAutoWidth | FormatOptionNoPrefix, ATTR_PROC_ID);
		mask.registerFormat (NULL, -14, AltQuestion | AltWide, format_owner_wide, ATTR_OWNER /*, "[????????????]"*/ );
		//mask.registerFormat(" ", "*bogus*", " ");  // force space
		mask.registerFormat (NULL,  11, AltQuestion | AltWide, format_q_date, ATTR_Q_DATE /*, "[????????????]"*/ );
		//mask.registerFormat(" ", "*bogus*", " ");  // force space
		mask.registerFormat (NULL,  12, AltQuestion | AltWide, format_cpu_time,
							 ATTR_JOB_REMOTE_USER_CPU /*, "[??????????]"*/);
		if (dash_run && ! dash_goodput) {
			mask_headings = (cputime) ? " ID\0 \0OWNER\0  SUBMITTED\0    CPU_TIME\0HOST(S)\0"
			                          : " ID\0 \0OWNER\0  SUBMITTED\0    RUN_TIME\0HOST(S)\0";
			//mask.registerFormat(" ", "*bogus*", " "); // force space
			// We send in ATTR_OWNER since we know it is always
			// defined, and we need to make sure
			// format_remote_host() is always called. We are
			// actually displaying ATTR_REMOTE_HOST if defined,
			// but we play some tricks if it isn't defined.
			mask.registerFormat ( NULL, 0, AltQuestion | AltWide, format_remote_host,
								  ATTR_OWNER /*, "[????????????????]"*/);
		} else {			// goodput
			mask_headings = (cputime) ? " ID\0 \0OWNER\0  SUBMITTED\0    CPU_TIME\0GOODPUT\0CPU_UTIL\0Mb/s\0"
			                          : " ID\0 \0OWNER\0  SUBMITTED\0    RUN_TIME\0GOODPUT\0CPU_UTIL\0Mb/s\0";
			mask.registerFormat (NULL, 8, AltQuestion | AltWide, format_goodput,
								 ATTR_JOB_STATUS /*, "[?????]"*/);
			mask.registerFormat (NULL, 9, AltQuestion | AltWide, format_cpu_util,
								 ATTR_JOB_REMOTE_USER_CPU /*, "[??????]"*/);
			mask.registerFormat (NULL, 7, AltQuestion | AltWide, format_mbps,
								 ATTR_BYTES_SENT /*, "[????]"*/);
		}
		//mask.registerFormat("\n", "*bogus*", "\n");  // force newline
		usingPrintMask = true;
	} else if (dash_globus) {
		mask_headings = " ID\0 \0OWNER\0STATUS\0MANAGER     HOST\0EXECUTABLE\0";
		mask.SetAutoSep(NULL," ",NULL,"\n");
		mask.registerFormat ("%4d.", 5, FormatOptionAutoWidth | FormatOptionNoSuffix, ATTR_CLUSTER_ID);
		mask.registerFormat ("%-3d", 3, FormatOptionAutoWidth | FormatOptionNoPrefix,  ATTR_PROC_ID);
		mask.registerFormat (NULL, -14, AltQuestion | AltWide, format_owner_wide, ATTR_OWNER /*, "[?]"*/ );
		mask.registerFormat(NULL, -8, AltQuestion | AltWide, format_globusStatus, ATTR_GLOBUS_STATUS /*, "[?]"*/ );
		if (widescreen) {
			mask.registerFormat(NULL, -30,  FormatOptionAlwaysCall | FormatOptionAutoWidth | FormatOptionNoTruncate,
				format_globusHostAndJM, ATTR_GRID_RESOURCE);
			mask.registerFormat("%v", -18, FormatOptionAutoWidth | FormatOptionNoTruncate, ATTR_JOB_CMD );
		} else {
			mask.registerFormat(NULL, 30, FormatOptionAlwaysCall,
				format_globusHostAndJM, ATTR_JOB_CMD);
			mask.registerFormat("%-18.18s", ATTR_JOB_CMD);
		}
		usingPrintMask = true;
	} else if( dash_grid ) {
		mask_headings = " ID\0 \0OWNER\0STATUS\0GRID->MANAGER    HOST\0GRID_JOB_ID\0";
		//mask.SetAutoSep("<","{","},",">\n"); // for testing.
		mask.SetAutoSep(NULL," ",NULL,"\n");
		mask.registerFormat ("%4d.", 5, FormatOptionAutoWidth | FormatOptionNoSuffix, ATTR_CLUSTER_ID);
		mask.registerFormat ("%-3d", 3, FormatOptionAutoWidth | FormatOptionNoPrefix, ATTR_PROC_ID);
		if (widescreen) {
			int excess = (console_width - (8+1 + 14+1 + 10+1 + 27+1 + 16));
			int w2 = 27 + MAX(0, (excess-8)/3);
			mask.registerFormat (NULL, -14, FormatOptionAutoWidth | FormatOptionNoTruncate, format_owner_wide, ATTR_OWNER);
			mask.registerFormat (NULL, -10, FormatOptionAutoWidth | FormatOptionNoTruncate, format_gridStatus, ATTR_JOB_STATUS);
			mask.registerFormat (NULL, -w2, FormatOptionAutoWidth | FormatOptionNoTruncate, format_gridResource, ATTR_GRID_RESOURCE);
			mask.registerFormat (NULL, 0, FormatOptionNoTruncate, format_gridJobId, ATTR_GRID_JOB_ID);
		} else {
			mask.registerFormat (NULL, -14, 0, format_owner_wide, ATTR_OWNER);
			mask.registerFormat (NULL, -10,0, format_gridStatus, ATTR_JOB_STATUS);
			mask.registerFormat ("%-27.27s", 0,0, format_gridResource, ATTR_GRID_RESOURCE);
			mask.registerFormat ("%-16.16s", 0,0, format_gridJobId, ATTR_GRID_JOB_ID);
		}
		usingPrintMask = true;
	} else if ( show_held ) {
		mask_headings = " ID\0 \0OWNER\0HELD_SINCE\0HOLD_REASON\0";
		mask.SetAutoSep(NULL," ",NULL,"\n");
		mask.registerFormat ("%4d.", 5, FormatOptionAutoWidth | FormatOptionNoSuffix, ATTR_CLUSTER_ID);
		mask.registerFormat ("%-3d", 3, FormatOptionAutoWidth | FormatOptionNoPrefix, ATTR_PROC_ID);
		mask.registerFormat (NULL, -14, AltQuestion | AltWide, format_owner_wide, ATTR_OWNER /*, "[????????????]"*/ );
		mask.registerFormat (NULL, 11, AltQuestion | AltWide, format_q_date,
							  ATTR_ENTERED_CURRENT_STATUS /*, "[????????????]"*/);
		if (widescreen) {
			mask.registerFormat("%v", -43, FormatOptionAutoWidth | FormatOptionNoTruncate, ATTR_HOLD_REASON );
		} else {
			mask.registerFormat("%-43.43s", ATTR_HOLD_REASON);
		}
		usingPrintMask = true;
	} else if (dash_autocluster && ! usingPrintMask) {
		mask_headings = " AUTO_ID\0JOB_COUNT\0";
		mask.SetAutoSep(NULL, " ", NULL, "\n");
		mask.registerFormat("%8d", 8, FormatOptionAutoWidth, ATTR_AUTO_CLUSTER_ID);
		mask.registerFormat("%9d", 9, FormatOptionAutoWidth, "JobCount");
		usingPrintMask = true;
		summarize = false;
		customHeadFoot = HF_NOSUMMARY;
	}

	if (mask_headings) {
		const char * pszz = mask_headings;
		size_t cch = strlen(pszz);
		while (cch > 0) {
#if 1
			mask.set_heading(pszz);
#else
			mask_head.Append(pszz);
#endif
			pszz += cch+1;
			cch = strlen(pszz);
		}
	}
}


// Given a list of jobs, do analysis for each job and print out the results.
//
static bool
print_jobs_analysis(ClassAdList & jobs, const char * source_label, Daemon * pschedd_daemon)
{
	// note: pschedd_daemon may be NULL.

		// check if job is being analyzed
	ASSERT( better_analyze );

	// build a job autocluster map
	if (verbose) { fprintf(stderr, "\nBuilding autocluster map of %d Jobs...", jobs.Length()); }
	JobClusterMap job_autoclusters;
	buildJobClusterMap(jobs, ATTR_AUTO_CLUSTER_ID, job_autoclusters);
	size_t cNoAuto = job_autoclusters[-1].size();
	size_t cAuto = job_autoclusters.size()-1; // -1 because size() counts the entry at [-1]
	if (verbose) { fprintf(stderr, "%d autoclusters and %d Jobs with no autocluster\n", (int)cAuto, (int)cNoAuto); }

	if (reverse_analyze) { 
		if (verbose && (startdAds.Length() > 1)) { fprintf(stderr, "Sorting %d Slots...", startdAds.Length()); }
		longest_slot_machine_name = 0;
		longest_slot_name = 0;
		if (startdAds.Length() == 1) {
			// if there is a single machine ad, then default to analysis
			if ( ! analyze_detail_level) analyze_detail_level = detail_analyze_each_sub_expr;
		} else {
			startdAds.Sort(SlotSort);
		}
		//if (verbose) { fprintf(stderr, "Done, longest machine name %d, longest slot name %d\n", longest_slot_machine_name, longest_slot_name); }
	} else {
		if (analyze_detail_level & detail_better) {
			analyze_detail_level |= detail_analyze_each_sub_expr | detail_always_analyze_req;
		}
	}

	// print banner for this source of jobs.
	printf ("\n\n-- %s\n", source_label);

	if (reverse_analyze) {
		int console_width = widescreen ? getDisplayWidth() : 80;
		if (startdAds.Length() <= 0) {
			// print something out?
		} else {
			bool analStartExpr = /*(better_analyze == 2) || */(analyze_detail_level > 0);
			if (summarize_anal || ! analStartExpr) {
				if (jobs.Length() <= 1) {
					jobs.Open();
					while (ClassAd *job = jobs.Next()) {
						int cluster_id = 0, proc_id = 0;
						job->LookupInteger(ATTR_CLUSTER_ID, cluster_id);
						job->LookupInteger(ATTR_PROC_ID, proc_id);
						printf("%d.%d: Analyzing matches for 1 job\n", cluster_id, proc_id);
					}
					jobs.Close();
				} else {
					int cNoAutoT = (int)job_autoclusters[-1].size();
					int cAutoclustersT = (int)job_autoclusters.size()-1; // -1 because [-1] is not actually an autocluster
					if (verbose) {
						printf("Analyzing %d jobs in %d autoclusters\n", jobs.Length(), cAutoclustersT+cNoAutoT);
					} else {
						printf("Analyzing matches for %d jobs\n", jobs.Length());
					}
				}
				std::string fmt;
				int name_width = MAX(longest_slot_machine_name+7, longest_slot_name);
				formatstr(fmt, "%%-%d.%ds", MAX(name_width, 16), MAX(name_width, 16));
				fmt += " %4s %12.12s %12.12s %10.10s\n";

				printf(fmt.c_str(), ""    , "Slot", "Slot's Req ", "  Job's Req ", "Both   ");
				printf(fmt.c_str(), "Name", "Type", "Matches Job", "Matches Slot", "Match %");
				printf(fmt.c_str(), "------------------------", "----", "------------", "------------", "----------");
			}
			startdAds.Open();
			while (ClassAd *slot = startdAds.Next()) {
				doSlotRunAnalysis(slot, job_autoclusters, pschedd_daemon, console_width);
			}
			startdAds.Close();
		}
	} else {
		if (summarize_anal) {
			if (single_machine) {
				printf("%s: Analyzing matches for %d slots\n", user_slot_constraint, startdAds.Length());
			} else {
				printf("Analyzing matches for %d slots\n", startdAds.Length());
			}
			const char * fmt = "%-13s %12s %12s %11s %11s %10s %9s %s\n";
			printf(fmt, "",              " Autocluster", "   Matches  ", "  Machine  ", "  Running  ", "  Serving ", "", "");
			printf(fmt, " JobId",        "Members/Idle", "Requirements", "Rejects Job", " Users Job ", "Other User", "Available", summarize_with_owner ? "Owner" : "");
			printf(fmt, "-------------", "------------", "------------", "-----------", "-----------", "----------", "---------", summarize_with_owner ? "-----" : "");
		}

		for (JobClusterMap::iterator it = job_autoclusters.begin(); it != job_autoclusters.end(); ++it) {
			int cJobsInCluster = (int)it->second.size();
			if (cJobsInCluster <= 0)
				continue;

			// for the the non-autocluster cluster, we have to eval these jobs individually
			int cJobsToEval = (it->first == -1) ? cJobsInCluster : 1;
			int cJobsToInc  = (it->first == -1) ? 1 : cJobsInCluster;
			for (int ii = 0; ii < cJobsToEval; ++ii) {
				ClassAd *job = it->second[ii];
				if (summarize_anal) {
					char achJobId[16], achAutocluster[16], achRunning[16];
					int cluster_id = 0, proc_id = 0;
					job->LookupInteger(ATTR_CLUSTER_ID, cluster_id);
					job->LookupInteger(ATTR_PROC_ID, proc_id);
					sprintf(achJobId, "%d.%d", cluster_id, proc_id);

					string owner;
					if (summarize_with_owner) job->LookupString(ATTR_OWNER, owner);
					if (owner.empty()) owner = "";

					int cIdle = 0, cRunning = 0;
					for (int jj = 0; jj < cJobsToInc; ++jj) {
						ClassAd * jobT = it->second[jj];
						int jobState = 0;
						if (jobT->LookupInteger(ATTR_JOB_STATUS, jobState)) {
							if (IDLE == jobState) 
								++cIdle;
							else if (TRANSFERRING_OUTPUT == jobState || RUNNING == jobState || SUSPENDED == jobState)
								++cRunning;
						}
					}

					if (it->first >= 0) {
						if (verbose) {
							sprintf(achAutocluster, "%d:%d/%d", it->first, cJobsToInc, cIdle);
						} else {
							sprintf(achAutocluster, "%d/%d", cJobsToInc, cIdle);
						}
					} else {
						achAutocluster[0] = 0;
					}

					anaCounters ac;
					doJobRunAnalysisToBuffer(job, ac, detail_always_analyze_req, true, false);
					const char * fmt = "%-13s %-12s %12d %11d %11s %10d %9d %s\n";

					achRunning[0] = 0;
					if (cRunning) { sprintf(achRunning, "%d/", cRunning); }
					sprintf(achRunning+strlen(achRunning), "%d", ac.machinesRunningUsersJobs);

					printf(fmt, achJobId, achAutocluster,
							ac.totalMachines - ac.fReqConstraint,
							ac.fOffConstraint,
							achRunning,
							ac.machinesRunningJobs - ac.machinesRunningUsersJobs,
							ac.available,
							owner.c_str());
				} else {
					doJobRunAnalysis(job, pschedd_daemon, analyze_detail_level);
				}
			}
		}
	}

	return true;
}

// callback function for processing a job from the Q query that just adds the 
// job into a ClassAdList.
static bool
AddToClassAdList(void * pv, ClassAd* ad) {
	ClassAdList * plist = (ClassAdList*)pv;
	plist->Insert(ad);
	return false; // return false to indicate we took ownership of the ad.
}

#ifdef HAVE_EXT_POSTGRESQL
static bool
show_db_queue( const char* quill_name, const char* db_ipAddr, const char* db_name, const char* query_password)
{
		// initialize counters
	idle = running = held = malformed = suspended = completed = removed = 0;

	ClassAdList jobs;
	CondorError errstack;

	std::string source_label;
	formatstr(source_label, "Quill: %s : %s : %s : ", quill_name, db_ipAddr, db_name);

	// choose a processing option for jobad's as the come off the wire.
	// for -long -xml and -analyze, we need to save off the ad in a ClassAdList
	// for -stream we print out the ad 
	// and for everything else, we 
	//
	buffer_line_processor pfnProcess;
	void *                pvProcess;
	if (dash_long || better_analyze) {
		pfnProcess = AddToClassAdList;
		pvProcess = &jobs;
	} else if (g_stream_results) {
		pfnProcess = process_and_print_job;
		pvProcess = NULL;

		// we are about to print out the jobads, so print an output header now.
		print_full_header(source_label.c_str());
	} else {
		// tj: 2013 refactor---
		// so we can compare the output of old vs. new code. keep both around for now.
		pfnProcess = /*use_old_code ? process_buffer_line_old : */ process_job_and_render_to_dag_map;
		pvProcess = &dag_map;
	}

	// fetch queue directly from the database

#ifdef HAVE_EXT_POSTGRESQL

		char *lastUpdate=NULL;
		char *dbconn=NULL;
		dbconn = getDBConnStr(quill_name, db_ipAddr, db_name, query_password);

		if( Q.fetchQueueFromDBAndProcess( dbconn,
											lastUpdate,
											pfnProcess, pvProcess,
											&errstack) != Q_OK ) {
			fprintf( stderr, 
					"\n-- Failed to fetch ads from db [%s] at database "
					"server %s\n%s\n",
					db_name, db_ipAddr, errstack.getFullText(true).c_str() );

			if(dbconn) {
				free(dbconn);
			}
			if(lastUpdate) {
				free(lastUpdate);
			}
			return false;
		}
		if(dbconn) {
			free(dbconn);
		}
		if(lastUpdate) {
			source_label += lastUpdate;
			free(lastUpdate);
		}
#else
		if (query_password) {} /* Done to suppress set-but-not-used warnings */
#endif /* HAVE_EXT_POSTGRESQL */
	if (pfnProcess || pvProcess) {}

	// if we streamed, then the results have already been printed out.
	// we just need to write the footer/summary
	if (g_stream_results) {
		print_full_footer();
		return true;
	}

	if (better_analyze) {
		return print_jobs_analysis(jobs, source_label.c_str(), NULL);
	}

	// at this point we either have a populated jobs list, or a populated dag_map
	// depending on which processing function we used in the query above.
	// for -long -xml or -analyze, we should have jobs, but no dag_map

	// if outputing dag format, we want to build the parent child links in the dag map
	// and then edit the text for jobs that are dag nodes.
	if (dash_dag && (dag_map.size() > 0)) {
		linkup_dag_nodes_in_dag_map(/*dag_map, dag_cluster_map*/);
		rewrite_output_for_dag_nodes_in_dag_map(/*dag_map*/);
	}

	// we want a header for this schedd if we are not showing the global queue OR if there is any data
	if ( ! global || dag_map.size() > 0 || jobs.Length() > 0) {
		print_full_header(source_label.c_str());
	}

	// print out jobs, either from the dag_map, or the jobs list, we expect only
	// one of these to be populated depending on which processing function we passed to the query
	if (dag_map.size() > 0) {
		print_dag_map(/*dag_map*/);
		// free the strings in the dag_map
		clear_dag_map(/*dag_map, dag_cluster_map*/);
	} else {
		jobs.Open();
		while(ClassAd *job = jobs.Next()) {
			process_and_print_job(NULL, job);
		}
		jobs.Close();
	}

	// print out summary line (if enabled) and/or xml closure
	// we want to print out a footer if we printed any data, OR if this is not 
	// a global query, so that the user can see that we did something.
	//
	if ( ! global || dag_map.size() > 0 || jobs.Length() > 0) {
		print_full_footer();
	}

	return true;
}
#endif // HAVE_EXT_POSTGRESQL


static void count_job(ClassAd *job)
{
	int status = 0;
	job->LookupInteger(ATTR_JOB_STATUS, status);
	switch (status)
	{
		case IDLE:                ++idle;      break;
		case TRANSFERRING_OUTPUT:
		case RUNNING:             ++running;   break;
		case SUSPENDED:           ++suspended; break;
		case COMPLETED:           ++completed; break;
		case REMOVED:             ++removed;   break;
		case HELD:                ++held;      break;
	}
}


static const char * render_job_text(ClassAd *job, std::string & result_text)
{
	if (use_xml) {
		sPrintAdAsXML(result_text, *job, attrs.isEmpty() ? NULL : &attrs);
	} else if (dash_long) {
		sPrintAd(result_text, *job, false, (dash_autocluster || attrs.isEmpty()) ? NULL : &attrs);
		result_text += "\n";
	} else if (better_analyze) {
		ASSERT(0); // should never get here.
	} else if (show_io) {
		buffer_io_display(result_text, job);
	} else if (usingPrintMask) {
		if (mask.IsEmpty()) return NULL;
		mask.display(result_text, job);
	} else {
		result_text += bufferJobShort(job);
	}
	return result_text.c_str();
}

static bool
process_and_print_job(void *, ClassAd *job)
{
	count_job(job);

	std::string result_text;
	render_job_text(job, result_text);
	if ( ! result_text.empty()) { printf("%s", result_text.c_str()); }
	return true;
}

#ifdef ALLOW_DASH_DAG

// insert a line of formatted output into the dag_map, so we can print out
// sorted results.
//
static bool
insert_job_output_into_dag_map(ClassAd * job, const char * job_output)
{
	clusterProcString * tempCPS = new clusterProcString;
	if (dash_autocluster) {
		const char * attr_id = ATTR_AUTO_CLUSTER_ID;
		if (dash_autocluster == CondorQ::fetch_GroupBy) attr_id = "Id";
		job->LookupInteger(attr_id, tempCPS->cluster );
		tempCPS->proc = 0;
	} else {
		job->LookupInteger( ATTR_CLUSTER_ID, tempCPS->cluster );
		job->LookupInteger( ATTR_PROC_ID, tempCPS->proc );
	}

	// If it's not a DAGMan job (and this includes the DAGMan process
	// itself), then set the dagman_cluster_id equal to cluster so that
	// it sorts properly against dagman jobs.
	char dagman_job_string[32];
	bool dci_initialized = false;
	if (!job->LookupString(ATTR_DAGMAN_JOB_ID, dagman_job_string, sizeof(dagman_job_string))) {
			// we failed to find an DAGManJobId string attribute,
			// let's see if we find one that is an integer.
		int temp_cluster = -1;
		if (!job->LookupInteger(ATTR_DAGMAN_JOB_ID,temp_cluster)) {
				// could not job DAGManJobId, so fall back on
				// just the regular job id
			tempCPS->dagman_cluster_id = tempCPS->cluster;
			tempCPS->dagman_proc_id    = tempCPS->proc;
			dci_initialized = true;
		} else {
				// in this case, we found DAGManJobId set as
				// an integer, not a string --- this means it is
				// the cluster id.
			tempCPS->dagman_cluster_id = temp_cluster;
			tempCPS->dagman_proc_id    = 0;
			dci_initialized = true;
		}
	} else {
		// We've gotten a string, probably something like "201.0"
		// we want to convert it to the numbers 201 and 0. To be safe, we
		// use atoi on either side of the period. We could just use
		// sscanf, but I want to know each fail case, so I can set them
		// to reasonable values.
		char *loc_period = strchr(dagman_job_string, '.');
		char *proc_string_start = NULL;
		if (loc_period != NULL) {
			*loc_period = 0;
			proc_string_start = loc_period+1;
		}
		if (isdigit(*dagman_job_string)) {
			tempCPS->dagman_cluster_id = atoi(dagman_job_string);
			dci_initialized = true;
		} else {
			// It must not be a cluster id, because it's not a number.
			tempCPS->dagman_cluster_id = tempCPS->cluster;
			dci_initialized = true;
		}

		if (proc_string_start != NULL && isdigit(*proc_string_start)) {
			tempCPS->dagman_proc_id = atoi(proc_string_start);
			dci_initialized = true;
		} else {
			tempCPS->dagman_proc_id = 0;
			dci_initialized = true;
		}
	}

	if ( ! dci_initialized) {
		return false;
	}

	tempCPS->string = strnewp(job_output);
	//PRAGMA_REMIND("tj: change tempCPS->string to a set of fields, so we can set column widths based on data.")

	// insert tempCPS (and job_output copy) into the dag_map.
	// this transfers ownership of both allocations, they will be freed in the code
	// that deletes the map.
	clusterProcMapper cpm(*tempCPS);
	std::pair<dag_map_type::iterator,bool> pp = dag_map.insert(
		std::pair<clusterProcMapper,clusterProcString*>(
			cpm, tempCPS ) );
	if( !pp.second ) {
		fprintf( stderr, "Error: Two clusters with the same ID.\n" );
		fprintf( stderr, "tempCPS: %d %d %d %d\n", tempCPS->dagman_cluster_id,
			tempCPS->dagman_proc_id, tempCPS->cluster, tempCPS->proc );
		dag_map_type::iterator ppp = dag_map.find(cpm);
		fprintf( stderr, "Comparing against: %d %d %d %d\n",
			ppp->second->dagman_cluster_id, ppp->second->dagman_proc_id,
			ppp->second->cluster, ppp->second->proc );
		//tj: 2013 -don't abort without printing jobs
		// exit( 1 );
		//maybe we should toss tempCPS into an orphan list rather than simply leaking it?
		return false;
	}

	// also insert into the dag_cluster_map
	//
	clusterIDProcIDMapper cipim(*tempCPS);
	std::pair<dag_cluster_map_type::iterator,bool> pq = dag_cluster_map.insert(
		std::pair<clusterIDProcIDMapper,clusterProcString*>(
			cipim,tempCPS ) );
	if( !pq.second ) {
		fprintf( stderr, "Error: Clusters have nonunique IDs.\n" );
		//tj: 2013 -don't abort without printing jobs
		//exit( 1 );
	}

	return true;
}

static bool
process_job_and_render_to_dag_map(void *,  ClassAd* job)
{
	count_job(job);

	ASSERT( ! g_stream_results);

	std::string result_text;
	render_job_text(job, result_text);
	insert_job_output_into_dag_map(job, result_text.c_str());
	return true;
}

#endif

typedef std::map<std::string, std::string> RESULT_MAP_TYPE;
RESULT_MAP_TYPE result_map;

static bool
process_job_to_map(void * pv,  ClassAd* job)
{
	RESULT_MAP_TYPE * pmap = (RESULT_MAP_TYPE *)pv;
	count_job(job);

	ASSERT( ! g_stream_results);

	std::string key;
	//PRAGMA_REMIND("TJ: this should be a sort of arbitrary keys, including numeric keys.")
	//PRAGMA_REMIND("TJ: fix to honor ascending/descending.")
	if ( ! group_by_keys.empty()) {
		for (size_t ii = 0; ii < group_by_keys.size(); ++ii) {
			std::string value;
			if (job->LookupString(group_by_keys[ii].expr.c_str(), value)) {
				key += value;
				key += "\n";
			}
		}
	}

	union {
		struct { int proc; int cluster; };
		long long id;
	} jobid;

	if (dash_autocluster) {
		const char * attr_id = ATTR_AUTO_CLUSTER_ID;
		if (dash_autocluster == CondorQ::fetch_GroupBy) attr_id = "Id";
		job->LookupInteger(attr_id, jobid.id);
	} else {
		job->LookupInteger( ATTR_CLUSTER_ID, jobid.cluster );
		job->LookupInteger( ATTR_PROC_ID, jobid.proc );
	}
	formatstr_cat(key, "%016llX", jobid.id);


	std::pair<RESULT_MAP_TYPE::iterator,bool> pp = pmap->insert(std::pair<std::string, std::string>(key, ""));
	if( ! pp.second ) {
		fprintf( stderr, "Error: Two results with the same ID.\n" );
		//tj: 2013 -don't abort without printing jobs
		// exit( 1 );
		return false;
	} else {
		render_job_text(job, pp.first->second);
	}

	return true;
}

void print_results(RESULT_MAP_TYPE & result_map)
{
	for(RESULT_MAP_TYPE::iterator it = result_map.begin(); it != result_map.end(); ++it) {
		//printf("%s", it->first.c_str()); 
		printf("%s", it->second.c_str());
	}
}

void clear_results(RESULT_MAP_TYPE & result_map)
{
	result_map.clear();
}


//PRAGMA_REMIND("Write this properly as a method on the sinful object.")

const char * summarize_sinful_for_display(std::string & addrsumy, const char * addr)
{
	addrsumy = addr;
	const char * quest = strchr(addr, '?');
	if ( ! quest) {
	   addrsumy = addr;
	   return addr;
	}
	
	addrsumy.clear();
	addrsumy.insert(0, addr, 0, quest - addr);
	addrsumy += "?...";
	return addrsumy.c_str();
}


// query SCHEDD or QUILLD daemon for jobs. and then print out the desired job info.
// this function handles -analyze, -streaming, -dag and all normal condor_q output
// when the source is a SCHEDD or QUILLD.
// TJ isn't sure that the QUILL daemon can use fast path, prior to the 2013 refactor, 
// the old code didn't try.
//
static bool
show_schedd_queue(const char* scheddAddress, const char* scheddName, const char* scheddMachine, int useFastPath)
{
	// initialize counters
	malformed = idle = running = held = completed = suspended = 0;

	std::string addr_summary;
	summarize_sinful_for_display(addr_summary, scheddAddress);
	std::string source_label;
	if (querySubmittors) {
		formatstr(source_label, "Submitter: %s : %s : %s", scheddName, addr_summary.c_str(), scheddMachine);
	} else {
		formatstr(source_label, "Schedd: %s : %s", scheddName, addr_summary.c_str());
	}

	if (verbose || dash_profile) {
		fprintf(stderr, "Fetching job ads...");
	}
	size_t cbBefore = 0;
	double tmBefore = 0;
	if (dash_profile) { cbBefore = ProcAPI::getBasicUsage(getpid(), &tmBefore); }

	// fetch queue from schedd
	ClassAdList jobs;  // this will get filled in for -long -xml and -analyze
	CondorError errstack;

	// choose a processing option for jobad's as the come off the wire.
	// for -long -xml and -analyze, we need to save off the ad in a ClassAdList
	// for -stream we print out the ad 
	// and for everything else, we 
	//
	buffer_line_processor pfnProcess = NULL;
	void *                pvProcess = NULL;
	if (dash_long || better_analyze) {
		pfnProcess = AddToClassAdList;
		pvProcess = &jobs;
	} else if (g_stream_results) {
		pfnProcess = process_and_print_job;
		// we are about to print out the jobads, so print an output header now.
		print_full_header(source_label.c_str());
	} else {
		if (dash_dag) {
		#ifdef ALLOW_DASH_DAG
			pfnProcess = process_job_and_render_to_dag_map;
			pvProcess = &dag_map;
		#else
			ASSERT(!dash_dag);
		#endif
		} else {
			pfnProcess = process_job_to_map;
			pvProcess = &result_map;
		}
	}

	int fetch_opts = 0;
	if (dash_autocluster) {
		fetch_opts = dash_autocluster;
	}
	bool use_v3 = dash_autocluster || param_boolean("CONDOR_Q_USE_V3_PROTOCOL", true);
	if ((useFastPath == 2) && !use_v3) {
		useFastPath = 1;
	}
	int fetchResult = Q.fetchQueueFromHostAndProcess(scheddAddress, attrs, fetch_opts, g_match_limit, pfnProcess, pvProcess, useFastPath, &errstack);
	if (fetchResult != Q_OK) {
		// The parse + fetch failed, print out why
		switch(fetchResult) {
		case Q_PARSE_ERROR:
		case Q_INVALID_CATEGORY:
			fprintf(stderr, "\n-- Parse error in constraint expression \"%s\"\n", user_job_constraint);
			break;
		case Q_UNSUPPORTED_OPTION_ERROR:
			fprintf(stderr, "\n-- Unsupported query option at: %s : %s\n", scheddAddress, scheddMachine);
			break;
		default:
			fprintf(stderr,
				"\n-- Failed to fetch ads from: %s : %s\n%s\n",
				scheddAddress, scheddMachine, errstack.getFullText(true).c_str() );
		}
		return false;
	}

	// if we streamed, then the results have already been printed out.
	// we just need to write the footer/summary
	if (g_stream_results) {
		print_full_footer();
		return true;
	}

	if (dash_profile) {
		profile_print(cbBefore, tmBefore, jobs.Length());
	} else if (verbose) {
		fprintf(stderr, " %d ads\n", jobs.Length());
	}

	if (better_analyze) {
		if (jobs.Length() > 1) {
			if (verbose) { fprintf(stderr, "Sorting %d Jobs...", jobs.Length()); }
			jobs.Sort(JobSort);
			if (verbose) { fprintf(stderr, "\n"); }
		}
		
		//PRAGMA_REMIND("TJ: shouldn't this be using scheddAddress instead of scheddName?")
		Daemon schedd(DT_SCHEDD, scheddName, pool ? pool->addr() : NULL );

		return print_jobs_analysis(jobs, source_label.c_str(), &schedd);
	}

	// at this point we either have a populated jobs list, or a populated dag_map
	// depending on which processing function we used in the query above.
	// for -long -xml or -analyze, we should have jobs, but no dag_map

	// if outputing dag format, we want to build the parent child links in the dag map
	// and then edit the text for jobs that are dag nodes.
	int cResults = (int)result_map.size();
	#ifdef ALLOW_DASH_DAG
	if (dash_dag) {
		cResults = (int)dag_map.size();
		if (cResults) {
			linkup_dag_nodes_in_dag_map(/*dag_map, dag_cluster_map*/);
			rewrite_output_for_dag_nodes_in_dag_map(/*dag_map*/);
		}
	}
	#endif

	// we want a header for this schedd if we are not showing the global queue OR if there is any data
	if ( ! global || cResults > 0 || jobs.Length() > 0) {
		print_full_header(source_label.c_str());
	}

	// print out jobs, either from the dag_map, or the jobs list, we expect only
	// one of these to be populated depending on which processing function we passed to the query
	if (cResults > 0) {
	#ifdef ALLOW_DASH_DAG
		if (dash_dag) {
			print_dag_map(/*dag_map*/);
			// free the strings in the dag_map
			clear_dag_map(/*dag_map, dag_cluster_map*/);
		} else
	#endif
		{
			print_results(result_map);
			clear_results(result_map);
		}
	} else {
		jobs.Open();
		while(ClassAd *job = jobs.Next()) {
			process_and_print_job(NULL, job);
		}
		jobs.Close();
	}

	// print out summary line (if enabled) and/or xml closure
	// we want to print out a footer if we printed any data, OR if this is not 
	// a global query, so that the user can see that we did something.
	//
	if ( ! global || cResults > 0 || jobs.Length() > 0) {
		print_full_footer();
	}

	return true;
}

// Read ads from a file, either in classad format, or userlog format.
//
static bool
show_file_queue(const char* jobads, const char* userlog)
{
	// initialize counters
	malformed = idle = running = held = completed = suspended = 0;

	// setup constraint variables to use in some of the later code.
	const char * constr = NULL;
	std::string constr_string; // to hold the return from ExprTreeToString()
	ExprTree *tree = NULL;
	Q.rawQuery(tree);
	if (tree) {
		constr_string = ExprTreeToString(tree);
		constr = constr_string.c_str();
		delete tree;
	}

	if (verbose || dash_profile) {
		fprintf(stderr, "Fetching job ads...");
	}

	size_t cbBefore = 0;
	double tmBefore = 0;
	if (dash_profile) { cbBefore = ProcAPI::getBasicUsage(getpid(), &tmBefore); }

	ClassAdList jobs;
	std::string source_label;

	if (jobads != NULL) {
		/* get the "q" from the job ads file */
		CondorQClassAdFileParseHelper jobads_file_parse_helper;
		if (!read_classad_file(jobads, jobs, &jobads_file_parse_helper, constr)) {
			return false;
		}
		// if we are doing analysis, print out the schedd name and address
		// that we got from the classad file.
		if (better_analyze && ! jobads_file_parse_helper.schedd_name.empty()) {
			const char *scheddName    = jobads_file_parse_helper.schedd_name.c_str();
			const char *scheddAddress = jobads_file_parse_helper.schedd_addr.c_str();
			const char *queryType     = jobads_file_parse_helper.is_submitter ? "Submitter" : "Schedd";
			formatstr(source_label, "%s: %s : %s", queryType, scheddName, scheddAddress);
			}
		
		/* The variable UseDB should be false in this branch since it was set 
			in main() because jobads_file had a good value. */

	} else if (userlog != NULL) {
		CondorID * JobIds = NULL;
		int cJobIds = constrID.size();
		if (cJobIds > 0) JobIds = &constrID[0];

		if (!userlog_to_classads(userlog, jobs, JobIds, cJobIds, constr)) {
			fprintf(stderr, "\nCan't open user log: %s\n", userlog);
			return false;
		}
		formatstr(source_label, "Userlog: %s", userlog);
	} else {
		ASSERT(jobads != NULL || userlog != NULL);
	}

	if (dash_profile) {
		profile_print(cbBefore, tmBefore, jobs.Length());
	} else if (verbose) {
		fprintf(stderr, " %d ads.\n", jobs.Length());
	}

	if (jobs.Length() > 1) {
		if (verbose) { fprintf(stderr, "Sorting %d Jobs...", jobs.Length()); }
		jobs.Sort(JobSort);

		if (dash_profile) {
			profile_print(cbBefore, tmBefore, jobs.Length(), false);
		} else if (verbose) {
			fprintf(stderr, "\n");
		}
	} else if (verbose) {
		fprintf(stderr, "\n");
	}

	if (better_analyze) {
		return print_jobs_analysis(jobs, source_label.c_str(), NULL);
	}

	// TJ: copied this from the top of init_output_mask
	if ( dash_run || dash_goodput || dash_globus || dash_grid )
		summarize = false;
	else if ((customHeadFoot&HF_NOSUMMARY) && ! show_held)
		summarize = false;

		// display the jobs from this submittor
	if( jobs.MyLength() != 0 || !global ) {
		print_full_header(source_label.c_str());

		jobs.Open();
		while(ClassAd *job = jobs.Next()) {
			process_and_print_job(NULL, job);
		}
		jobs.Close();

		print_full_footer();
	}

	return true;
}


static bool is_slot_name(const char * psz)
{
	// if string contains only legal characters for a slotname, then assume it's a slotname.
	// legal characters are a-zA-Z@_\.\-
	while (*psz) {
		if (*psz != '-' && *psz != '.' && *psz != '@' && *psz != '_' && ! isalnum(*psz))
			return false;
		++psz;
	}
	return true;
}

static void
setupAnalysis()
{
	CondorQuery	query(STARTD_AD);
	int			rval;
	char		buffer[64];
	char		*preq;
	ClassAd		*ad;
	string		remoteUser;
	int			index;

	
	if (verbose || dash_profile) {
		fprintf(stderr, "Fetching Machine ads...");
	}

	double tmBefore = 0.0;
	size_t cbBefore = 0;
	if (dash_profile) { cbBefore = ProcAPI::getBasicUsage(getpid(), &tmBefore); }

	// fetch startd ads
    if (machineads_file != NULL) {
        if (!read_classad_file(machineads_file, startdAds, NULL, NULL)) {
            exit ( 1 );
        }
    } else {
		if (user_slot_constraint) {
			if (is_slot_name(user_slot_constraint)) {
				std::string str;
				formatstr(str, "(%s==\"%s\") || (%s==\"%s\")",
					      ATTR_NAME, user_slot_constraint, ATTR_MACHINE, user_slot_constraint);
				query.addORConstraint (str.c_str());
				single_machine = true;
			} else {
				query.addANDConstraint (user_slot_constraint);
			}
		}
        rval = Collectors->query (query, startdAds);
        if( rval != Q_OK ) {
            fprintf( stderr , "Error:  Could not fetch startd ads\n" );
            exit( 1 );
        }
    }

	if (dash_profile) {
		profile_print(cbBefore, tmBefore, startdAds.Length());
	} else if (verbose) {
		fprintf(stderr, " %d ads.\n", startdAds.Length());
	}

	// fetch user priorities, and propagate the user priority values into the machine ad's
	// we fetch user priorities either directly from the negotiator, or from a file
	// treat a userprios_file of "" as a signal to ignore user priority.
	//
	int cPrios = 0;
	if (userprios_file || analyze_with_userprio) {
		if (verbose || dash_profile) {
			fprintf(stderr, "Fetching user priorities...");
		}

		if (userprios_file != NULL) {
			cPrios = read_userprio_file(userprios_file, prioTable);
			if (cPrios < 0) {
				//PRAGMA_REMIND("TJ: don't exit here, just analyze without userprio information")
				exit (1);
			}
		} else {
			// fetch submittor prios
			cPrios = fetchSubmittorPriosFromNegotiator(prioTable);
			if (cPrios < 0) {
				//PRAGMA_REMIND("TJ: don't exit here, just analyze without userprio information")
				exit(1);
			}
		}

		if (dash_profile) {
			profile_print(cbBefore, tmBefore, cPrios);
		}
		else if (verbose) {
			fprintf(stderr, " %d submitters\n", cPrios);
		}
		/* TJ: do we really want to do this??
		if (cPrios <= 0) {
			printf( "Warning:  Found no submitters\n" );
		}
		*/


		// populate startd ads with remote user prios
		startdAds.Open();
		while( ( ad = startdAds.Next() ) ) {
			if( ad->LookupString( ATTR_REMOTE_USER , remoteUser ) ) {
				if( ( index = findSubmittor( remoteUser.c_str() ) ) != -1 ) {
					sprintf( buffer , "%s = %f" , ATTR_REMOTE_USER_PRIO , 
								prioTable[index].prio );
					ad->Insert( buffer );
				}
			}
			#if defined(ADD_TARGET_SCOPING)
			ad->AddTargetRefs( TargetJobAttrs );
			#endif
		}
		startdAds.Close();
	}
	

	// setup condition expressions
    sprintf( buffer, "MY.%s > MY.%s", ATTR_RANK, ATTR_CURRENT_RANK );
    ParseClassAdRvalExpr( buffer, stdRankCondition );

    sprintf( buffer, "MY.%s >= MY.%s", ATTR_RANK, ATTR_CURRENT_RANK );
    ParseClassAdRvalExpr( buffer, preemptRankCondition );

	sprintf( buffer, "MY.%s > TARGET.%s + %f", ATTR_REMOTE_USER_PRIO, 
			ATTR_SUBMITTOR_PRIO, PriorityDelta );
	ParseClassAdRvalExpr( buffer, preemptPrioCondition ) ;

	// setup preemption requirements expression
	if( !( preq = param( "PREEMPTION_REQUIREMENTS" ) ) ) {
		fprintf( stderr, "\nWarning:  No PREEMPTION_REQUIREMENTS expression in"
					" config file --- assuming FALSE\n\n" );
		ParseClassAdRvalExpr( "FALSE", preemptionReq );
	} else {
		if( ParseClassAdRvalExpr( preq , preemptionReq ) ) {
			fprintf( stderr, "\nError:  Failed parse of "
				"PREEMPTION_REQUIREMENTS expression: \n\t%s\n", preq );
			exit( 1 );
		}
#if defined(ADD_TARGET_SCOPING)
		ExprTree *tmp_expr = AddTargetRefs( preemptionReq, TargetJobAttrs );
		delete preemptionReq;
		preemptionReq = tmp_expr;
#endif
		free( preq );
	}

}


static int
fetchSubmittorPriosFromNegotiator(ExtArray<PrioEntry> & prios)
{
	AttrList	al;
	char  	attrName[32], attrPrio[32];
  	char  	name[128];
  	float 	priority;

	Daemon	negotiator( DT_NEGOTIATOR, NULL, pool ? pool->addr() : NULL );

	// connect to negotiator
	Sock* sock;

	if (!(sock = negotiator.startCommand( GET_PRIORITY, Stream::reli_sock, 0))) {
		fprintf( stderr, "Error: Could not connect to negotiator (%s)\n",
				 negotiator.fullHostname() );
		return -1;
	}

	sock->end_of_message();
	sock->decode();
	if( !getClassAdNoTypes(sock, al) || !sock->end_of_message() ) {
		fprintf( stderr, 
				 "Error:  Could not get priorities from negotiator (%s)\n",
				 negotiator.fullHostname() );
		return -1;
	}
	sock->close();
	delete sock;


	int cPrios = 0;
	while( true ) {
		sprintf( attrName , "Name%d", cPrios+1 );
		sprintf( attrPrio , "Priority%d", cPrios+1 );

		if( !al.LookupString( attrName, name, sizeof(name) ) || 
			!al.LookupFloat( attrPrio, priority ) )
			break;

		prios[cPrios].name = name;
		prios[cPrios].prio = priority;
		++cPrios;
	}

	return cPrios;
}

// parse lines of the form "attrNNNN = value" and return attr, NNNN and value as separate fields.
static int parse_userprio_line(const char * line, std::string & attr, std::string & value)
{
	int id = 0;

	// skip over the attr part
	const char * p = line;
	while (*p && isspace(*p)) ++p;

	// parse prefixNNN and set id=NNN and attr=prefix
	const char * pattr = p;
	while (*p) {
		if (isdigit(*p)) {
			attr.assign(pattr, p-pattr);
			id = atoi(p);
			while (isdigit(*p)) ++p;
			break;
		} else if  (isspace(*p)) {
			break;
		}
		++p;
	}
	if (id <= 0)
		return -1;

	// parse = with or without whitespace.
	while (isspace(*p)) ++p;
	if (*p != '=')
		return -1;
	++p;
	while (isspace(*p)) ++p;

	// parse value, remove "" from around strings 
	if (*p == '"')
		value.assign(p+1,strlen(p)-2);
	else
		value = p;
	return id;
}

static int read_userprio_file(const char *filename, ExtArray<PrioEntry> & prios)
{
	int cPrios = 0;

	FILE *file = safe_fopen_wrapper_follow(filename, "r");
	if (file == NULL) {
		fprintf(stderr, "Can't open file of user priorities: %s\n", filename);
		return -1;
	} else {
		int lineno = 0;
		while (const char * line = getline_trim(file, lineno)) {
			std::string attr, value;
			int id = parse_userprio_line(line, attr, value);
			if (id <= 0) {
				// there are valid attributes that we want to ignore that return -1 from
				// this call, so just skip them
				continue;
			}

			if (attr == "Priority") {
				float priority = atof(value.c_str());
				prios[id].prio = priority;
				cPrios = MAX(cPrios, id);
			} else if (attr == "Name") {
				prios[id].name = value;
				cPrios = MAX(cPrios, id);
			}
		}
		fclose(file);
	}
	return cPrios;
}

static void
doJobRunAnalysis(ClassAd *request, Daemon *schedd, int details)
{
	//PRAGMA_REMIND("TJ: move this code out of this function so it doesn't output once per job")
	if (schedd) {
		MyString buf;
		warnScheddLimits(schedd, request, buf);
		printf("%s", buf.Value());
	}

	anaCounters ac;
	printf("%s", doJobRunAnalysisToBuffer(request, ac, details, false, verbose));
}

static void append_to_fail_list(std::string & list, const char * entry, size_t limit)
{
	if ( ! limit || list.size() < limit) {
		if (list.size() > 1) { list += ", "; }
		list += entry;
	} else if (list.size() > limit) {
		if (limit > 50) {
			list.erase(limit-3);
			list += "...";
		} else {
			list.erase(limit);
		}
	}
}


static const char *
doJobRunAnalysisToBuffer(ClassAd *request, anaCounters & ac, int details, bool noPrio, bool showMachines)
{
	char	owner[128];
	std::string  user;
	char	buffer[128];
	ClassAd	*offer;
	classad::Value	eval_result;
	bool	val;
	int		cluster, proc;
	int		jobState;
	int		niceUser;
	int		universe;
	int		verb_width = 100;
	int		jobMatched = false;
	std::string job_status = "";

	bool	alwaysReqAnalyze = (details & detail_always_analyze_req) != 0;
	bool	analEachReqClause = (details & detail_analyze_each_sub_expr) != 0;
	bool	useNewPrettyReq = true;
	bool	showJobAttrs = analEachReqClause && ! (details & detail_dont_show_job_attrs);
	bool	withoutPrio = false;

	ac.clear();
	return_buff[0]='\0';

#if defined(ADD_TARGET_SCOPING)
	request->AddTargetRefs( TargetMachineAttrs );
#endif

	if (analyze_memory_usage) {
		int num_skipped = 0;
		QuantizingAccumulator mem_use(0,0);
		const classad::ExprTree* tree = request->LookupExpr(analyze_memory_usage);
		if (tree) {
			AddExprTreeMemoryUse(tree, mem_use, num_skipped);
		}
		size_t raw_mem, quantized_mem, num_allocs;
		raw_mem = mem_use.Value(&quantized_mem, &num_allocs);

		sprintf(return_buff, "\nMemory usage for '%s' is %d bytes from %d allocations requesting %d bytes (%d expr nodes skipped)\n",
				analyze_memory_usage, (int)quantized_mem, (int)num_allocs, (int)raw_mem, num_skipped);
		return return_buff;
	}

	if( !request->LookupString( ATTR_OWNER , owner, sizeof(owner) ) ) return "Nothing here.\n";
	if( !request->LookupInteger( ATTR_NICE_USER , niceUser ) ) niceUser = 0;
	request->LookupString(ATTR_USER, user);

	int last_match_time=0, last_rej_match_time=0;
	request->LookupInteger(ATTR_LAST_MATCH_TIME, last_match_time);
	request->LookupInteger(ATTR_LAST_REJ_MATCH_TIME, last_rej_match_time);

	request->LookupInteger( ATTR_CLUSTER_ID, cluster );
	request->LookupInteger( ATTR_PROC_ID, proc );
	request->LookupInteger( ATTR_JOB_STATUS, jobState );
	request->LookupBool( ATTR_JOB_MATCHED, jobMatched );
	if (jobState == RUNNING || jobState == TRANSFERRING_OUTPUT || jobState == SUSPENDED) {
		job_status = "Request is running.";
	}
	if (jobState == HELD) {
		job_status = "Request is held.";
		MyString hold_reason;
		request->LookupString( ATTR_HOLD_REASON, hold_reason );
		if( hold_reason.Length() ) {
			job_status += "\n\nHold reason: ";
			job_status += hold_reason.Value();
		}
	}
	if (jobState == REMOVED) {
		job_status = "Request is removed.";
	}
	if (jobState == COMPLETED) {
		job_status = "Request is completed.";
	}
	if (jobMatched) {
		job_status = "Request has been matched.";
	}

	// if we already figured out the job status, and we haven't been asked to analyze requirements anyway
	// we are done.
	if ( ! job_status.empty() && ! alwaysReqAnalyze) {
		sprintf(return_buff, "---\n%03d.%03d:  %s\n\n" , cluster, proc, job_status.c_str());
		return return_buff;
	}

	// 
	int ixSubmittor = -1;
	if (noPrio) {
		withoutPrio = true;
	} else {
		findSubmittor(fixSubmittorName(user.c_str(), niceUser));
		if (ixSubmittor < 0) {
			withoutPrio = true;
		} else {
			request->Assign(ATTR_SUBMITTOR_PRIO, prioTable[ixSubmittor].prio);
		}
	}

	startdAds.Open();

	std::string fReqConstraintStr("[");
	std::string fOffConstraintStr("[");
	std::string fOfflineStr("[");
	std::string fPreemptPrioStr("[");
	std::string fPreemptReqTestStr("[");
	std::string fRankCondStr("[");

	while( ( offer = startdAds.Next() ) ) {
		// 0.  info from machine
		ac.totalMachines++;
		offer->LookupString( ATTR_NAME , buffer, sizeof(buffer) );
		//if( verbose ) { strcat(return_buff, buffer); strcat(return_buff, " "); }

		// 1. Request satisfied? 
		if( !IsAHalfMatch( request, offer ) ) {
			//if( verbose ) strcat(return_buff, "Failed request constraint\n");
			ac.fReqConstraint++;
			if (showMachines) append_to_fail_list(fReqConstraintStr, buffer, verb_width);
			continue;
		} 

		// 2. Offer satisfied? 
		if ( !IsAHalfMatch( offer, request ) ) {
			//if( verbose ) strcat( return_buff, "Failed offer constraint\n");
			ac.fOffConstraint++;
			if (showMachines) append_to_fail_list(fOffConstraintStr, buffer, verb_width);
			continue;
		}	

		int offline = 0;
		if( offer->EvalBool( ATTR_OFFLINE, NULL, offline ) && offline ) {
			ac.fOffline++;
			if (showMachines) append_to_fail_list(fOfflineStr, buffer, verb_width);
			continue;
		}

		// 3. Is there a remote user?
		string remoteUser;
		if( !offer->LookupString( ATTR_REMOTE_USER, remoteUser ) ) {
			// no remote user
			if( EvalExprTree( stdRankCondition, offer, request, eval_result ) &&
				eval_result.IsBooleanValue(val) && val ) {
				// both sides satisfied and no remote user
				//if( verbose ) strcat(return_buff, "Available\n");
				ac.available++;
				continue;
			} else {
				// no remote user and std rank condition failed
			  if (last_rej_match_time != 0) {
				ac.fRankCond++;
				if (showMachines) append_to_fail_list(fRankCondStr, buffer, verb_width);
				//if( verbose ) strcat( return_buff, "Failed rank condition: MY.Rank > MY.CurrentRank\n");
				continue;
			  } else {
				ac.available++; // tj: is this correct?
				//PRAGMA_REMIND("TJ: move this out of the machine iteration loop?")
				if (job_status.empty()) {
					job_status = "Request has not yet been considered by the matchmaker.";
				}
				continue;
			  }
			}
		}

		// if we get to here, there is a remote user, if we don't have access to user priorities
		// we can't decide whether we should be able to preempt other users, so we are done.
		ac.machinesRunningJobs++;
		if (withoutPrio) {
			if (user == remoteUser) {
				++ac.machinesRunningUsersJobs;
			} else {
				append_to_fail_list(fPreemptPrioStr, buffer, verb_width); // borrow preempt prio list
			}
			continue;
		}

		// machines running your jobs will never preempt for your job.
		if (user == remoteUser) {
			++ac.machinesRunningUsersJobs;

		// 4. Satisfies preemption priority condition?
		} else if( EvalExprTree( preemptPrioCondition, offer, request, eval_result ) &&
			eval_result.IsBooleanValue(val) && val ) {

			// 5. Satisfies standard rank condition?
			if( EvalExprTree( stdRankCondition, offer , request , eval_result ) &&
				eval_result.IsBooleanValue(val) && val )  
			{
				//if( verbose ) strcat( return_buff, "Available\n");
				ac.available++;
				continue;
			} else {
				// 6.  Satisfies preemption rank condition?
				if( EvalExprTree( preemptRankCondition, offer, request, eval_result ) &&
					eval_result.IsBooleanValue(val) && val )
				{
					// 7.  Tripped on PREEMPTION_REQUIREMENTS?
					if( EvalExprTree( preemptionReq, offer , request , eval_result ) &&
						eval_result.IsBooleanValue(val) && !val ) 
					{
						ac.fPreemptReqTest++;
						if (showMachines) append_to_fail_list(fPreemptReqTestStr, buffer, verb_width);
						/*
						if( verbose ) {
							sprintf_cat( return_buff,
									"%sCan preempt %s, but failed "
									"PREEMPTION_REQUIREMENTS test\n",
									buffer,
									 remoteUser.c_str());
						}
						*/
						continue;
					} else {
						// not held
						/*
						if( verbose ) {
							sprintf_cat( return_buff,
								"Available (can preempt %s)\n", remoteUser.c_str());
						}
						*/
						ac.available++;
					}
				} else {
					// failed 6 and 5, but satisfies 4; so have priority
					// but not better or equally preferred than current
					// customer
					// NOTE: In practice this often indicates some
					// unknown problem.
				  if (last_rej_match_time != 0) {
					ac.fRankCond++;
				  } else {
					if (job_status.empty()) {
						job_status = "Request has not yet been considered by the matchmaker.";
					}
				  }
				}
			} 
		} else {
			ac.fPreemptPrioCond++;
			append_to_fail_list(fPreemptPrioStr, buffer, verb_width);
			/*
			if( verbose ) {
				sprintf_cat( return_buff, "Insufficient priority to preempt %s\n", remoteUser.c_str() );
			}
			*/
			continue;
		}
	}
	startdAds.Close();

	if (summarize_anal)
		return return_buff;

	fReqConstraintStr += "]";
	fOffConstraintStr += "]";
	fOfflineStr += "]";
	fPreemptPrioStr += "]";
	fPreemptReqTestStr += "]";
	fRankCondStr += "]";

	if ( ! job_status.empty()) {
		sprintf(return_buff, "---\n%03d.%03d:  %s\n\n" , cluster, proc, job_status.c_str());
	}

	if ( ! noPrio && ixSubmittor < 0) {
		sprintf(return_buff+strlen(return_buff), "User priority for %s is not available, attempting to analyze without it.\n", user.c_str());
	}

	sprintf( return_buff + strlen(return_buff),
		 "---\n%03d.%03d:  Run analysis summary.  Of %d machines,\n"
		 "  %5d are rejected by your job's requirements %s\n"
		 "  %5d reject your job because of their own requirements %s\n",
		cluster, proc, ac.totalMachines,
		ac.fReqConstraint, showMachines  ? fReqConstraintStr.c_str() : "",
		ac.fOffConstraint, showMachines ? fOffConstraintStr.c_str() : "");

	if (withoutPrio) {
		sprintf( return_buff + strlen(return_buff),
			"  %5d match and are already running your jobs %s\n",
			ac.machinesRunningUsersJobs, "");

		sprintf( return_buff + strlen(return_buff),
			"  %5d match but are serving other users %s\n",
			ac.machinesRunningJobs - ac.machinesRunningUsersJobs, showMachines ? fPreemptPrioStr.c_str() : "");

		if (ac.fOffline > 0) {
			sprintf( return_buff + strlen(return_buff),
				"  %5d match but are currently offline %s\n",
				ac.fOffline, showMachines ? fOfflineStr.c_str() : "");
		}

		sprintf( return_buff + strlen(return_buff),
			"  %5d are available to run your job\n",
			ac.available );
	} else {
		sprintf( return_buff + strlen(return_buff),
			 "  %5d match and are already running one of your jobs%s\n"
			 "  %5d match but are serving users with a better priority in the pool%s %s\n"
			 "  %5d match but reject the job for unknown reasons %s\n"
			 "  %5d match but will not currently preempt their existing job %s\n"
			 "  %5d match but are currently offline %s\n"
			 "  %5d are available to run your job\n",
			ac.machinesRunningUsersJobs, "",
			ac.fPreemptPrioCond, niceUser ? "(*)" : "", showMachines ? fPreemptPrioStr.c_str() : "",
			ac.fRankCond, showMachines ? fRankCondStr.c_str() : "",
			ac.fPreemptReqTest,  showMachines ? fPreemptReqTestStr.c_str() : "",
			ac.fOffline, showMachines ? fOfflineStr.c_str() : "",
			ac.available );

		sprintf(return_buff + strlen(return_buff), 
			 "  %s has a priority of %9.3f\n", prioTable[ixSubmittor].name.Value(), prioTable[ixSubmittor].prio);
	}

	if (last_match_time) {
		time_t t = (time_t)last_match_time;
		sprintf( return_buff, "%s\tLast successful match: %s",
				 return_buff, ctime(&t) );
	} else if (last_rej_match_time) {
		strcat( return_buff, "\tNo successful match recorded.\n" );
	}
	if (last_rej_match_time > last_match_time) {
		time_t t = (time_t)last_rej_match_time;
		string timestring(ctime(&t));
		string rej_str="\tLast failed match: " + timestring + '\n';
		strcat(return_buff, rej_str.c_str());
		string rej_reason;
		if (request->LookupString(ATTR_LAST_REJ_MATCH_REASON, rej_reason)) {
			rej_str="\tReason for last match failure: " + rej_reason + '\n';
			strcat(return_buff, rej_str.c_str());	
		}
	}

	if( niceUser ) {
		sprintf( return_buff, 
				 "%s\n\t(*)  Since this is a \"nice-user\" request, this request "
				 "has a\n\t     very low priority and is unlikely to preempt other "
				 "requests.\n", return_buff );
	}
			

	if( ac.fReqConstraint == ac.totalMachines ) {
		strcat( return_buff, "\nWARNING:  Be advised:\n");
		strcat( return_buff, "   No resources matched request's constraints\n");
        if (!better_analyze) {
            ExprTree *reqExp;
            sprintf( return_buff, "%s   Check the %s expression below:\n\n" , 
                     return_buff, ATTR_REQUIREMENTS );
            if( !(reqExp = request->LookupExpr( ATTR_REQUIREMENTS) ) ) {
                sprintf( return_buff, "%s   ERROR:  No %s expression found" ,
                         return_buff, ATTR_REQUIREMENTS );
            } else {
				sprintf( return_buff, "%s%s = %s\n\n", return_buff,
						 ATTR_REQUIREMENTS, ExprTreeToString( reqExp ) );
            }
        }
	}

	if (better_analyze && ((ac.fReqConstraint > 0) || alwaysReqAnalyze)) {

		// first analyze the Requirements expression against the startd ads.
		//
		std::string suggest_buf = "";
		std::string pretty_req = "";
		analyzer.GetErrors(true); // true to clear errors
		analyzer.AnalyzeJobReqToBuffer( request, startdAds, suggest_buf, pretty_req );
		if ((int)suggest_buf.size() > SHORT_BUFFER_SIZE)
		   suggest_buf.erase(SHORT_BUFFER_SIZE, string::npos);

		bool requirements_is_false = (suggest_buf == "Job ClassAd Requirements expression evaluates to false\n\n");

		if ( ! useNewPrettyReq) {
			strcat(return_buff, pretty_req.c_str());
		}
		pretty_req = "";

		if (useNewPrettyReq) {
			classad::ExprTree* tree = request->LookupExpr(ATTR_REQUIREMENTS);
			if (tree) {
				int console_width = widescreen ? getDisplayWidth() : 80;
				const int indent = 4;
				PrettyPrintExprTree(tree, pretty_req, indent, console_width);
				strcat(return_buff, "\nThe Requirements expression for your job is:\n\n");
				std::string spaces; spaces.insert(0, indent, ' ');
				strcat(return_buff, spaces.c_str());
				strcat(return_buff, pretty_req.c_str());
				strcat(return_buff, "\n\n");
				pretty_req = "";
			}
		}

		// then capture the values of MY attributes refereced by the expression
		// also capture the value of TARGET attributes if there is only a single ad.
		if (showJobAttrs) {
			std::string attrib_values = "";
			attrib_values = "Your job defines the following attributes:\n\n";
			StringList trefs;
			AddReferencedAttribsToBuffer(request, ATTR_REQUIREMENTS, trefs, "    ", attrib_values);
			strcat(return_buff, attrib_values.c_str());
			attrib_values = "";

			if (single_machine || startdAds.Length() == 1) { 
				startdAds.Open(); 
				while (ClassAd * ptarget = startdAds.Next()) {
					attrib_values = "\n";
					AddTargetAttribsToBuffer(trefs, request, ptarget, "    ", attrib_values);
					strcat(return_buff, attrib_values.c_str());
				}
				startdAds.Close();
			}
			strcat(return_buff, "\n");
		}

		// TJ's experimental analysis (now with more anal)
		if (analEachReqClause || requirements_is_false) {
			std::string subexpr_detail;
			anaFormattingOptions fmt = { widescreen ? getConsoleWindowSize() : 80, details, "Requirements", "Job", "Slot" };
			AnalyzeRequirementsForEachTarget(request, ATTR_REQUIREMENTS, startdAds, subexpr_detail, fmt);
			strcat(return_buff, "The Requirements expression for your job reduces to these conditions:\n\n");
			strcat(return_buff, subexpr_detail.c_str());
		}

		// write the analysis/suggestions to the return buffer
		//
		strcat(return_buff, "\nSuggestions:\n\n");
		strcat(return_buff, suggest_buf.c_str());
	}

	if( ac.fOffConstraint == ac.totalMachines ) {
        strcat(return_buff, "\nWARNING:  Be advised:\n   Request did not match any resource's constraints\n");
	}

    if (better_analyze) {
        std::string buffer_string = "";
        char ana_buffer[SHORT_BUFFER_SIZE];
        if( ac.fOffConstraint > 0 ) {
            buffer_string = "";
            analyzer.GetErrors(true); // true to clear errors
            analyzer.AnalyzeJobAttrsToBuffer( request, startdAds, buffer_string );
            strncpy( ana_buffer, buffer_string.c_str( ), SHORT_BUFFER_SIZE);
			ana_buffer[SHORT_BUFFER_SIZE-1] = '\0';
            strcat( return_buff, ana_buffer );
        }
    }

	request->LookupInteger( ATTR_JOB_UNIVERSE, universe );
	bool uses_matchmaking = false;
	MyString resource;
	switch(universe) {
			// Known valid
		case CONDOR_UNIVERSE_STANDARD:
		case CONDOR_UNIVERSE_JAVA:
		case CONDOR_UNIVERSE_VANILLA:
			break;

			// Unknown
		case CONDOR_UNIVERSE_PARALLEL:
		case CONDOR_UNIVERSE_VM:
			break;

			// Maybe
		case CONDOR_UNIVERSE_GRID:
			/* We may be able to detect when it's valid.  Check for existance
			 * of "$$(FOO)" style variables in the classad. */
			request->LookupString(ATTR_GRID_RESOURCE, resource);
			if ( strstr(resource.Value(),"$$") ) {
				uses_matchmaking = true;
				break;
			}  
			if (!uses_matchmaking) {
				sprintf( return_buff, "%s\nWARNING: Analysis is only meaningful for Grid universe jobs using matchmaking.\n", return_buff);
			}
			break;

			// Specific known bad
		case CONDOR_UNIVERSE_MPI:
			sprintf( return_buff, "%s\nWARNING: Analysis is meaningless for MPI universe jobs.\n", return_buff );
			break;

			// Specific known bad
		case CONDOR_UNIVERSE_SCHEDULER:
			/* Note: May be valid (although requiring a different algorithm)
			 * starting some time in V6.7. */
			sprintf( return_buff, "%s\nWARNING: Analysis is meaningless for Scheduler universe jobs.\n", return_buff );
			break;

			// Unknown
			/* These _might_ be meaningful, especially if someone adds a 
			 * universe but fails to update this code. */
		//case CONDOR_UNIVERSE_PIPE:
		//case CONDOR_UNIVERSE_LINDA:
		//case CONDOR_UNIVERSE_MAX:
		//case CONDOR_UNIVERSE_MIN:
		//case CONDOR_UNIVERSE_PVM:
		//case CONDOR_UNIVERSE_PVMD:
		default:
			sprintf( return_buff, "%s\nWARNING: Job universe unknown.  Analysis may not be meaningful.\n", return_buff );
			break;
	}


	//printf("%s",return_buff);
	return return_buff;
}


static void
doSlotRunAnalysis(ClassAd *slot, JobClusterMap & clusters, Daemon * /*schedd*/, int console_width)
{
	printf("%s", doSlotRunAnalysisToBuffer(slot, clusters, console_width));
}

static const char *
doSlotRunAnalysisToBuffer(ClassAd *slot, JobClusterMap & clusters, int console_width)
{
	bool analStartExpr = /*(better_analyze == 2) ||*/ (analyze_detail_level > 0);
	bool showSlotAttrs = ! (analyze_detail_level & detail_dont_show_job_attrs);
	anaFormattingOptions fmt = { console_width, analyze_detail_level, "START", "Slot", "Cluster" };

	return_buff[0] = 0;

#if defined(ADD_TARGET_SCOPING)
	slot->AddTargetRefs(TargetJobAttrs);
#endif

	std::string slotname = "";
	slot->LookupString(ATTR_NAME , slotname);

	int offline = 0;
	if (slot->EvalBool(ATTR_OFFLINE, NULL, offline) && offline) {
		sprintf(return_buff, "%-24.24s  is offline\n", slotname.c_str());
		return return_buff;
	}

	const char * slot_type = "Stat";
	bool is_dslot = false, is_pslot = false;
	if (slot->LookupBool("DynamicSlot", is_dslot) && is_dslot) slot_type = "Dyn";
	else if (slot->LookupBool("PartitionableSlot", is_pslot) && is_pslot) slot_type = "Part";

	int cTotalJobs = 0;
	int cUniqueJobs = 0;
	int cReqConstraint = 0;
	int cOffConstraint = 0;
	int cBothMatch = 0;

	ClassAdListDoesNotDeleteAds jobs;
	for (JobClusterMap::iterator it = clusters.begin(); it != clusters.end(); ++it) {
		int cJobsInCluster = (int)it->second.size();
		if (cJobsInCluster <= 0)
			continue;

		// for the the non-autocluster cluster, we have to eval these jobs individually
		int cJobsToEval = (it->first == -1) ? cJobsInCluster : 1;
		int cJobsToInc  = (it->first == -1) ? 1 : cJobsInCluster;

		cTotalJobs += cJobsInCluster;

		for (int ii = 0; ii < cJobsToEval; ++ii) {
			ClassAd *job = it->second[ii];

			jobs.Insert(job);
			cUniqueJobs += 1;

			#if defined(ADD_TARGET_SCOPING)
			job->AddTargetRefs(TargetMachineAttrs);
			#endif

			// 2. Offer satisfied?
			bool offer_match = IsAHalfMatch(slot, job);
			if (offer_match) {
				cOffConstraint += cJobsToInc;
			}

			// 1. Request satisfied?
			if (IsAHalfMatch(job, slot)) {
				cReqConstraint += cJobsToInc;
				if (offer_match) {
					cBothMatch += cJobsToInc;
				}
			}
		}
	}

	if ( ! summarize_anal && analStartExpr) {

		sprintf(return_buff, "\n-- Slot: %s : Analyzing matches for %d Jobs in %d autoclusters\n", 
				slotname.c_str(), cTotalJobs, cUniqueJobs);

		std::string pretty_req = "";
		static std::string prev_pretty_req;
		classad::ExprTree* tree = slot->LookupExpr(ATTR_REQUIREMENTS);
		if (tree) {
			PrettyPrintExprTree(tree, pretty_req, 4, console_width);

			tree = slot->LookupExpr(ATTR_START);
			if (tree) {
				pretty_req += "\n\n    START is ";
				PrettyPrintExprTree(tree, pretty_req, 4, console_width);
			}

			if (prev_pretty_req.empty() || prev_pretty_req != pretty_req) {
				strcat(return_buff, "\nThe Requirements expression for this slot is\n\n    ");
				strcat(return_buff, pretty_req.c_str());
				strcat(return_buff, "\n\n");
				// uncomment this line to print out Machine requirements only when it changes.
				//prev_pretty_req = pretty_req;
			}
			pretty_req = "";

			// then capture the values of MY attributes refereced by the expression
			// also capture the value of TARGET attributes if there is only a single ad.
			if (showSlotAttrs) {
				std::string attrib_values = "";
				attrib_values = "This slot defines the following attributes:\n\n";
				StringList trefs;
				AddReferencedAttribsToBuffer(slot, ATTR_REQUIREMENTS, trefs, "    ", attrib_values);
				strcat(return_buff, attrib_values.c_str());
				attrib_values = "";

				if (jobs.Length() == 1) {
					jobs.Open();
					while(ClassAd *job = jobs.Next()) {
						attrib_values = "\n";
						AddTargetAttribsToBuffer(trefs, slot, job, "    ", attrib_values);
						strcat(return_buff, attrib_values.c_str());
					}
					jobs.Close();
				}
				//strcat(return_buff, "\n");
				strcat(return_buff, "\nThe Requirements expression for this slot reduces to these conditions:\n\n");
			}
		}

		std::string subexpr_detail;
		AnalyzeRequirementsForEachTarget(slot, ATTR_REQUIREMENTS, (ClassAdList&)jobs, subexpr_detail, fmt);
		strcat(return_buff, subexpr_detail.c_str());

		//formatstr(subexpr_detail, "%-5.5s %8d\n", "[ALL]", cOffConstraint);
		//strcat(return_buff, subexpr_detail.c_str());

		formatstr(pretty_req, "\n%s: Run analysis summary of %d jobs.\n"
			"%5d (%.2f %%) match both slot and job requirements.\n"
			"%5d match the requirements of this slot.\n"
			"%5d have job requirements that match this slot.\n",
			slotname.c_str(), cTotalJobs,
			cBothMatch, cTotalJobs ? (100.0 * cBothMatch / cTotalJobs) : 0.0,
			cOffConstraint, 
			cReqConstraint);
		strcat(return_buff, pretty_req.c_str());
		pretty_req = "";

	} else {
		char fmt[sizeof("%-nnn.nnns %-4s %12d %12d %10.2f\n")];
		int name_width = MAX(longest_slot_machine_name+7, longest_slot_name);
		sprintf(fmt, "%%-%d.%ds", MAX(name_width, 16), MAX(name_width, 16));
		strcat(fmt, " %-4s %12d %12d %10.2f\n");
		sprintf(return_buff, fmt, slotname.c_str(), slot_type, 
				cOffConstraint, cReqConstraint, 
				cTotalJobs ? (100.0 * cBothMatch / cTotalJobs) : 0.0);
	}

	jobs.Clear();

	if (better_analyze) {
		std::string ana_buffer = "";
		strcat(return_buff, ana_buffer.c_str());
	}

	return return_buff;
}


static	void
buildJobClusterMap(ClassAdList & jobs, const char * attr, JobClusterMap & autoclusters)
{
	autoclusters.clear();

	jobs.Open();
	while(ClassAd *job = jobs.Next()) {

		int acid = -1;
		if (job->LookupInteger(attr, acid)) {
			//std::map<int, ClassAdListDoesNotDeleteAds>::iterator it;
			autoclusters[acid].push_back(job);
		} else {
			// stick auto-clusterless jobs into the -1 slot.
			autoclusters[-1].push_back(job);
		}

	}
	jobs.Close();
}


static int
findSubmittor( const char *name ) 
{
	MyString 	sub(name);
	int			last = prioTable.getlast();
	
	for(int i = 0 ; i <= last ; i++ ) {
		if( prioTable[i].name == sub ) return i;
	}

	//prioTable[last+1].name = sub;
	//prioTable[last+1].prio = 0.5;
	//return last+1;

	return -1;
}


static const char*
fixSubmittorName( const char *name, int niceUser )
{
	static 	bool initialized = false;
	static	char * uid_domain = 0;
	static	char buffer[128];

	if( !initialized ) {
		uid_domain = param( "UID_DOMAIN" );
		if( !uid_domain ) {
			fprintf( stderr, "Error: UID_DOMAIN not found in config file\n" );
			exit( 1 );
		}
		initialized = true;
	}

    // potential buffer overflow! Hao
	if( strchr( name , '@' ) ) {
		sprintf( buffer, "%s%s%s", 
					niceUser ? NiceUserName : "",
					niceUser ? "." : "",
					name );
		return buffer;
	} else {
		sprintf( buffer, "%s%s%s@%s", 
					niceUser ? NiceUserName : "",
					niceUser ? "." : "",
					name, uid_domain );
		return buffer;
	}

	return NULL;
}


static bool read_classad_file(const char *filename, ClassAdList &classads, ClassAdFileParseHelper* pparse_help, const char * constr)
{
	bool success = false;

	FILE* file = safe_fopen_wrapper_follow(filename, "r");
	if (file == NULL) {
		fprintf(stderr, "Can't open file of job ads: %s\n", filename);
		return false;
	} else {
		CondorClassAdFileParseHelper generic_helper("\n");
		if ( ! pparse_help)
			pparse_help = &generic_helper;

		for (;;) {
			ClassAd* classad = new ClassAd();

			int error;
			bool is_eof;
			int cAttrs = classad->InsertFromFile(file, is_eof, error, pparse_help);

			bool include_classad = cAttrs > 0 && error >= 0;
			if (include_classad && constr) {
				classad::Value val;
				if (classad->EvaluateExpr(constr,val)) {
					if ( ! val.IsBooleanValueEquiv(include_classad)) {
						include_classad = false;
					}
				}
			}
			if (include_classad) {
				classads.Insert(classad);
			} else {
				delete classad;
			}

			if (is_eof) {
				success = true;
				break;
			}
			if (error < 0) {
				success = false;
				break;
			}
		}

		fclose(file);
	}
	return success;
}


#ifdef HAVE_EXT_POSTGRESQL

/* get the quill address for the quillName specified */
static QueryResult getQuillAddrFromCollector(const char *quill_name, std::string & quill_addr) {
	QueryResult query_result = Q_OK;
	char		query_constraint[1024];
	CondorQuery	quillQuery(QUILL_AD);
	ClassAdList quillList;
	ClassAd		*ad;

	sprintf (query_constraint, "%s == \"%s\"", ATTR_NAME, quill_name);
	quillQuery.addORConstraint (query_constraint);

	query_result = Collectors->query ( quillQuery, quillList );

	quillList.Open();	
	while ((ad = quillList.Next())) {
		ad->LookupString(ATTR_MY_ADDRESS, quill_addr);
	}
	quillList.Close();
	return query_result;
}


static char * getDBConnStr(char const *&quill_name,
                           char const *&databaseIp,
                           char const *&databaseName,
                           char const *&query_password) {
	char            *host, *port, *dbconn;
	const char *ptr_colon;
	char            *tmpquillname, *tmpdatabaseip, *tmpdatabasename, *tmpquerypassword;
	int             len, tmp1, tmp2, tmp3;

	if((!quill_name && !(tmpquillname = param("QUILL_NAME"))) ||
	   (!databaseIp && !(tmpdatabaseip = param("QUILL_DB_IP_ADDR"))) ||
	   (!databaseName && !(tmpdatabasename = param("QUILL_DB_NAME"))) ||
	   (!query_password && !(tmpquerypassword = param("QUILL_DB_QUERY_PASSWORD")))) {
		fprintf( stderr, "Error: Could not find database related parameter\n");
		fprintf(stderr, "\n");
		print_wrapped_text("Extra Info: "
                       "The most likely cause for this error "
                       "is that you have not defined "
                       "QUILL_NAME/QUILL_DB_IP_ADDR/"
                       "QUILL_DB_NAME/QUILL_DB_QUERY_PASSWORD "
                       "in the condor_config file.  You must "
						   "define this variable in the config file", stderr);

		exit( 1 );
	}

	if(!quill_name) {
		quill_name = tmpquillname;
	}
	if(!databaseIp) {
		if(tmpdatabaseip[0] != '<') {
				//2 for the two brackets and 1 for the null terminator
			char *tstr = (char *) malloc(strlen(tmpdatabaseip)+3);
			sprintf(tstr, "<%s>", tmpdatabaseip);
			free(tmpdatabaseip);
			databaseIp = tstr;
		}
		else {
			databaseIp = tmpdatabaseip;
		}
	}

	if(!databaseName) {
		databaseName = tmpdatabasename;
	}
	if(!query_password) {
		query_password = tmpquerypassword;
	}

	tmp1 = strlen(databaseName);
	tmp2 = strlen(query_password);
	len = strlen(databaseIp);

		//the 6 is for the string "host= " or "port= "
		//the rest is a subset of databaseIp so a size of
		//databaseIp is more than enough
	host = (char *) malloc((len+6) * sizeof(char));
	port = (char *) malloc((len+6) * sizeof(char));

		//here we break up the ipaddress:port string and assign the
		//individual parts to separate string variables host and port
	ptr_colon = strchr((char *)databaseIp, ':');
	strcpy(host, "host=");
	strncat(host,
			databaseIp+1,
			ptr_colon - databaseIp -1);
	strcpy(port, "port=");
	strcat(port, ptr_colon+1);
	port[strlen(port)-1] = '\0';

		//tmp3 is the size of dbconn - its size is estimated to be
		//(2 * len) for the host/port part, tmp1 + tmp2 for the
		//password and dbname part and 1024 as a cautiously
		//overestimated sized buffer
	tmp3 = (2 * len) + tmp1 + tmp2 + 1024;
	dbconn = (char *) malloc(tmp3 * sizeof(char));
	sprintf(dbconn, "%s %s user=quillreader password=%s dbname=%s",
			host, port, query_password, databaseName);

	free(host);
	free(port);
	return dbconn;
}

static void exec_db_query(const char *quill_name, const char *db_ipAddr, const char *db_name, const char *query_password) {
	char *dbconn=NULL;
	
	dbconn = getDBConnStr(quill_name, db_ipAddr, db_name, query_password);

	printf ("\n\n-- Quill: %s : %s : %s\n", quill_name, 
			db_ipAddr, db_name);

	if (avgqueuetime) {
		Q.rawDBQuery(dbconn, AVG_TIME_IN_QUEUE);
	}
	if(dbconn) {
		free(dbconn);
	}

}

static void freeConnectionStrings() {
	squillName.clear();
	quillName = NULL;
	squillMachine.clear();
	quillMachine = NULL;
	sdbIpAddr.clear();
	dbIpAddr = NULL;
	sdbName.clear();
	dbName = NULL;
	squeryPassword.clear();
	queryPassword = NULL;
	quillAddr.clear();
}

static void quillCleanupOnExit(int n)
{
	freeConnectionStrings();
}

#endif /* HAVE_EXT_POSTGRESQL */

void warnScheddLimits(Daemon *schedd,ClassAd *job,MyString &result_buf) {
	if( !schedd ) {
		return;
	}
	ASSERT( job );

	ClassAd *ad = schedd->daemonAd();
	if (ad) {
		bool exhausted = false;
		ad->LookupBool("SwapSpaceExhausted", exhausted);
		if (exhausted) {
			result_buf.formatstr_cat("WARNING -- this schedd is not running jobs because it believes that doing so\n");
			result_buf.formatstr_cat("           would exhaust swap space and cause thrashing.\n");
			result_buf.formatstr_cat("           Set RESERVED_SWAP to 0 to tell the scheduler to skip this check\n");
			result_buf.formatstr_cat("           Or add more swap space.\n");
			result_buf.formatstr_cat("           The analysis code does not take this into consideration\n");
		}

		int maxJobsRunning 	= -1;
		int totalRunningJobs= -1;

		ad->LookupInteger( ATTR_MAX_JOBS_RUNNING, maxJobsRunning);
		ad->LookupInteger( ATTR_TOTAL_RUNNING_JOBS, totalRunningJobs);

		if ((maxJobsRunning > -1) && (totalRunningJobs > -1) && 
			(maxJobsRunning == totalRunningJobs)) { 
			result_buf.formatstr_cat("WARNING -- this schedd has hit the MAX_JOBS_RUNNING limit of %d\n", maxJobsRunning);
			result_buf.formatstr_cat("       to run more concurrent jobs, raise this limit in the config file\n");
			result_buf.formatstr_cat("       NOTE: the matchmaking analysis does not take the limit into consideration\n");
		}

		int status = -1;
		job->LookupInteger(ATTR_JOB_STATUS,status);
		if( status != RUNNING && status != TRANSFERRING_OUTPUT && status != SUSPENDED ) {

			int universe = -1;
			job->LookupInteger(ATTR_JOB_UNIVERSE,universe);

			char const *schedd_requirements_attr = NULL;
			switch( universe ) {
			case CONDOR_UNIVERSE_SCHEDULER:
				schedd_requirements_attr = ATTR_START_SCHEDULER_UNIVERSE;
				break;
			case CONDOR_UNIVERSE_LOCAL:
				schedd_requirements_attr = ATTR_START_LOCAL_UNIVERSE;
				break;
			}

			if( schedd_requirements_attr ) {
				MyString schedd_requirements_expr;
				ExprTree *expr = ad->LookupExpr(schedd_requirements_attr);
				if( expr ) {
					schedd_requirements_expr = ExprTreeToString(expr);
				}
				else {
					schedd_requirements_expr = "UNDEFINED";
				}

				int req = 0;
				if( !ad->EvalBool(schedd_requirements_attr,job,req) ) {
					result_buf.formatstr_cat("WARNING -- this schedd's policy %s failed to evalute for this job.\n",schedd_requirements_expr.Value());
				}
				else if( !req ) {
					result_buf.formatstr_cat("WARNING -- this schedd's policy %s evalutes to false for this job.\n",schedd_requirements_expr.Value());
				}
			}
		}
	}
}

// !!! ENTRIES IN THIS TABLE MUST BE SORTED BY THE FIRST FIELD !!
static const CustomFormatFnTableItem LocalPrintFormats[] = {
	{ "CPU_TIME",        ATTR_JOB_REMOTE_USER_CPU, format_cpu_time, NULL },
	{ "JOB_DESCRIPTION", ATTR_JOB_CMD, format_job_description, ATTR_JOB_DESCRIPTION "\0MATCH_EXP_" ATTR_JOB_DESCRIPTION "\0" },
	{ "JOB_ID",          ATTR_CLUSTER_ID, format_job_id, ATTR_PROC_ID "\0" },
	{ "JOB_STATUS",      ATTR_JOB_STATUS, format_job_status_char, ATTR_LAST_SUSPENSION_TIME "\0" ATTR_TRANSFERRING_INPUT "\0" ATTR_TRANSFERRING_OUTPUT "\0" ATTR_TRANSFER_QUEUED "\0" },
	{ "JOB_STATUS_RAW",  ATTR_JOB_STATUS, format_job_status_raw, NULL },
	{ "JOB_UNIVERSE",    ATTR_JOB_UNIVERSE, format_job_universe, NULL },
	{ "MEMORY_USAGE",    ATTR_IMAGE_SIZE, format_memory_usage, ATTR_MEMORY_USAGE "\0" },
	{ "OWNER",           ATTR_OWNER, format_owner_wide, ATTR_NICE_USER "\0" ATTR_DAGMAN_JOB_ID "\0" ATTR_DAG_NODE_NAME "\0" },
	{ "QDATE",           ATTR_Q_DATE, format_q_date, NULL },
	{ "READABLE_BYTES",  ATTR_BYTES_RECVD, format_readable_bytes, NULL },
	{ "READABLE_KB",     ATTR_REQUEST_DISK, format_readable_kb, NULL },
	{ "READABLE_MB",     ATTR_REQUEST_MEMORY, format_readable_mb, NULL },
	// PRAGMA_REMIND("format_remote_host is using ATTR_OWNER because it is StringCustomFormat, it should be AlwaysCustomFormat and ATTR_REMOTE_HOST
	{ "REMOTE_HOST",     ATTR_OWNER, format_remote_host, ATTR_JOB_UNIVERSE "\0" ATTR_REMOTE_HOST "\0" ATTR_EC2_REMOTE_VM_NAME "\0" ATTR_GRID_RESOURCE "\0" },
};
static const CustomFormatFnTable LocalPrintFormatsTable = SORTED_TOKENER_TABLE(LocalPrintFormats);

static int set_print_mask_from_stream(
	const char * streamid,
	bool is_filename,
	StringList & attrs) // out
{
	std::string where_expr;
	std::string messages;
	printmask_aggregation_t aggregation;

	SimpleInputStream * pstream = NULL;

	FILE *file = NULL;
	if (MATCH == strcmp("-", streamid)) {
		pstream = new SimpleFileInputStream(stdin, false);
	} else if (is_filename) {
		file = safe_fopen_wrapper_follow(streamid, "r");
		if (file == NULL) {
			fprintf(stderr, "Can't open select file: %s\n", streamid);
			return -1;
		}
		pstream = new SimpleFileInputStream(file, true);
	} else {
		pstream = new StringLiteralInputStream(streamid);
	}
	ASSERT(pstream);

	int err = SetAttrListPrintMaskFromStream(
					*pstream,
					LocalPrintFormatsTable,
					mask,
					customHeadFoot,
					aggregation,
					group_by_keys,
					where_expr,
					attrs,
					messages);
	delete pstream; pstream = NULL;
	if ( ! err) {
		usingPrintMask = true;
		if ( ! where_expr.empty()) {
			user_job_constraint = mask.store(where_expr.c_str());
			if (Q.addAND (user_job_constraint) != Q_OK) {
				formatstr_cat(messages, "WHERE expression is not valid: %s\n", user_job_constraint);
			}
		}
		if (aggregation) {
			if (aggregation == PR_COUNT_UNIQUE) {
				dash_autocluster = CondorQ::fetch_GroupBy;
			} else if (aggregation == PR_FROM_AUTOCLUSTER) {
				dash_autocluster = CondorQ::fetch_DefaultAutoCluster;
			}
		} else {
			// make sure that the projection has ClusterId and ProcId.
			if ( ! attrs.contains_anycase(ATTR_CLUSTER_ID)) attrs.append(ATTR_CLUSTER_ID);
			if ( ! attrs.contains_anycase(ATTR_PROC_ID)) attrs.append(ATTR_PROC_ID);
			// if we are generating a summary line, make sure that we have JobStatus
			if ( ! (customHeadFoot & HF_NOSUMMARY) && ! attrs.contains_anycase(ATTR_JOB_STATUS)) attrs.append(ATTR_JOB_STATUS);
		}
	}
	if ( ! messages.empty()) { fprintf(stderr, "%s", messages.c_str()); }
	return err;
}
