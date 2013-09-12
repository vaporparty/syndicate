#include <wdd-main.h>
#include <daemon-config.h>


void* start_watchdog_daemon(void* cls) {
    daemon_config *dc = (daemon_config*)cls;
    int port = dc->watchdog_daemon_port;
    shared_ptr<WDDaemonHandler> handler(new WDDaemonHandler());
    shared_ptr<TProcessor> processor(new WDDaemonProcessor(handler));
    shared_ptr<TServerTransport> serverTransport(new TServerSocket(port));
    shared_ptr<TTransportFactory> transportFactory(new TFramedTransportFactory());
    shared_ptr<TProtocolFactory> protocolFactory(new TBinaryProtocolFactory());

    //We are running a thread pool server.
    int nr_cpus = sysconf(_SC_NPROCESSORS_ONLN);
    int worker_count = nr_cpus - 1;
    shared_ptr<ThreadManager> threadManager = ThreadManager::newSimpleThreadManager(worker_count);
    shared_ptr<ThreadFactory> threadFactory(new PosixThreadFactory());
    threadManager->threadFactory(threadFactory);
    threadManager->start();
    TThreadedServer server(processor, serverTransport, transportFactory, protocolFactory);
    server.serve();
    return NULL;
}

int main(int argc, char* argv[]) {
    daemon_config *dc = get_daemon_config("watchdog.conf", NULL);
    pthread_t tid;
    if (pthread_create(&tid, NULL, start_watchdog_daemon, dc) < 0) {
	perror("pthread_create");
	exit(-1);
    }
    if (pthread_join(tid, NULL) < 0) {
	perror("pthread_join");
	exit(-1);
    }
    exit(0);
}
