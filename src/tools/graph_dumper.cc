#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <fstream>
#include <boost/optional.hpp>

#include "road_graph.hh"
#include "multimodal_graph.hh"
#include "pgsql_importer.hh"

#include <sys/resource.h>
#include <malloc.h>

using namespace Tempus;

// current heap usage (allocated minus freed), including overhead
static size_t heap_usage;
// total request size
static size_t heap_total_rsize;
// total allocated size, including malloc overhead
static size_t heap_total_size;

void* operator new(size_t s)
{
    void * p = malloc( s );
    size_t rs = *((size_t*)p - 1);
    heap_usage += rs;
    heap_total_rsize += s;
    heap_total_size += rs;
    return p;
}

void* operator new[](size_t s)
{
    void * p = malloc( s );
    size_t rs = *((size_t*)p - 1);
    heap_usage += rs;
    heap_total_rsize += s;
    heap_total_size += rs;
    return p;
}

void operator delete( void* p ) noexcept
{
    size_t rs = *((size_t*)p - 1);
    heap_usage -= rs;
    free(p);
}

void operator delete[]( void* p ) noexcept
{
    size_t rs = *((size_t*)p - 1);
    heap_usage -= rs;
    free(p);
}

struct MemInfo {
    size_t rss;
    size_t data;
};

MemInfo get_memory_usage()
{
    size_t dummy, rss, data;
    std::ifstream ifs( "/proc/self/statm" );
    ifs >> dummy >> rss >> dummy >> dummy >> dummy >> data;
    MemInfo info;
    info.rss = rss * 4 * 1024;
    info.data = data * 4 * 1024;
    return info;
#if 0
    rusage r;
    getrusage( RUSAGE_SELF, &r );
    float real_size = r.ru_maxrss / 1024.;
#endif
}

int main( int argc, char* argv[] )
{
    std::string dbconn = "dbname=tempus_test_db";
    std::string dump_file = "dump.bin";
    if ( argc == 2 ) {
        dbconn = argv[1];
        if ( dbconn == "-h" || dbconn == "--help" ) {
            std::cout << "Arguments: db_connection_parameters dump_file" << std::endl;
            return 1;
        }
    }
    else if ( argc == 3 ) {
        dbconn = argv[1];
        dump_file = argv[2];
    }

    MemInfo info;

    info = get_memory_usage();
    size_t real_size = info.rss /1024 /1024;
    size_t data_size = info.data /1024 /1024;
    std::cerr << "RSS: " << real_size << "MB" << std::endl;
    std::cerr << "Data: " << data_size << "MB" << std::endl;

    {
        std::unique_ptr<Multimodal::Graph> graph;

        using namespace boost::graph;

        heap_usage = 0;
        TextProgression progression;
        {
            PQImporter importer( dbconn );
            graph = importer.import_graph( progression, /* consistency = */ false );
        }

        info = get_memory_usage();
        real_size = info.rss /1024 /1024;
        data_size = info.data /1024 /1024;
        std::cerr << "RSS: " << real_size << "MB" << std::endl;
        std::cerr << "Data: " << data_size << "MB" << std::endl;

        std::cerr << "Heap usage: " << (heap_usage/1024/1024) << "MB" << std::endl;
        std::cerr << "Total requested size: " << (heap_total_rsize/1024/1024) << "MB" << std::endl;
        std::cerr << "Total size: " << (heap_total_size/1024/1024) << "MB" << std::endl;

        float frag = (1.0 - float(heap_usage) / info.rss) * 100.0;
        std::cerr << "Fragmentation: " << frag << "%" << std::endl;

        
        std::cout << "dumping to " << dump_file << " ... " << std::endl;
        dump_graph( *graph, dump_file );
    }

    std::cout << "reloading ... " << std::endl;
    std::unique_ptr<Multimodal::Graph> graph( reload_graph_from_dump( dump_file ) );

    info = get_memory_usage();
    real_size = info.rss /1024 /1024;
    data_size = info.data /1024 /1024;
    std::cerr << "RSS: " << real_size << "MB" << std::endl;
    std::cerr << "Data: " << data_size << "MB" << std::endl;

    std::cerr << "Heap usage: " << (heap_usage/1024/1024) << "MB" << std::endl;
}