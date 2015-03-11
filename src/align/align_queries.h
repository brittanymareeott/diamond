/****
Copyright (c) 2014, University of Tuebingen
All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
****
Author: Benjamin Buchfink
****/

#ifndef ALIGN_QUERIES_H_
#define ALIGN_QUERIES_H_

#if __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 3)
#include <parallel/algorithm>
#else
#include "../util/merge_sort.h"
#endif
#include "../search/trace_pt_buffer.h"
#include "../util/map.h"
#include "align_read.h"
#include "../util/task_queue.h"

using std::vector;

struct Output_piece
{
	Output_piece():
		text (0),
		size (0),
		empty (true)
	{ }
	Output_piece(char *text, size_t size):
		text (text),
		size (size),
		empty (false)
	{ }
	operator bool() const
	{ return empty == false; }
	char *text;
	size_t size;
	bool empty;
};

struct Output_writer
{
	Output_writer(Output_stream* f):
		f_ (f)
	{ }
	void operator()(const Output_piece &p)
	{
		if(p.text != 0) {
			f_->write(p.text, p.size);
			free(p.text);
		}
	}
private:
	Output_stream* const f_;
};

template<typename _val, typename _locr, typename _locl, unsigned _d>
void align_queries(typename Trace_pt_list<_locr,_locl>::iterator begin,
		typename Trace_pt_list<_locr,_locl>::iterator end,
		Output_buffer<_val> &buffer,
		Statistics &st)
{
	typedef Map<typename vector<hit<_locr,_locl> >::iterator,typename hit<_locr,_locl>::template Query_id<_d> > Map_t;
	Map_t hits (begin, end);
	typename Map_t::Iterator i = hits.begin();
	while(i.valid() && !exception_state()) {
		align_read<_val,_locr,_locl>(buffer, st, i.begin(), i.end());
		++i;
	}
}

template<typename _locr, typename _locl>
struct Query_fetcher
{
	Query_fetcher(typename Trace_pt_buffer<_locr,_locl>::Vector &trace_pts):
		trace_pts_ (trace_pts)
	{ }
private:
	typename Trace_pt_buffer<_locr,_locl>::Vector &trace_pts_;
};

template<typename _val, typename _locr, typename _locl>
void align_queries2(typename Trace_pt_buffer<_locr,_locl>::Vector &trace_pts, Output_stream* output_file)
{
	//const size_t max_size=65536,max_segments=4096,min_segments=program_options::threads()*4;
	const size_t max_size=4096,max_segments=4096,min_segments=program_options::threads()*4;
	if(trace_pts.size() == 0)
		return;
	vector<size_t> p;
	switch(query_contexts()) {
	case 1:
		p = map_partition(trace_pts.begin(), trace_pts.end(), hit<_locr,_locl>::template query_id<1>, max_size, max_segments, min_segments);
		break;
	case 2:
		p = map_partition(trace_pts.begin(), trace_pts.end(), hit<_locr,_locl>::template query_id<2>, max_size, max_segments, min_segments);
		break;
	case 6:
		p = map_partition(trace_pts.begin(), trace_pts.end(), hit<_locr,_locl>::template query_id<6>, max_size, max_segments, min_segments);
		break;
	}

	const size_t n = p.size() - 1;
	/*for(unsigned i=0;i<n;++i) {
		printf("%lu\n",p[i+1]-p[i]);
	}*/

	Output_writer writer (output_file);
	Task_queue<Output_piece,Output_writer> queue (n, program_options::threads()*10, writer);

#pragma omp parallel
	{
		Statistics st;
		size_t i;
		Void_callback cb;
		while(queue.get(i, cb) && !exception_state()) {
			try {
				size_t begin = p[i], end = p[i+1];
				Output_buffer<_val> *buffer = ref_header.n_blocks > 1 ? new Temp_output_buffer<_val> () : new Output_buffer<_val> ();
				if(query_contexts() == 6)
					align_queries<_val,_locr,_locl,6>(trace_pts, begin, end, *buffer, st);
				else if(query_contexts() == 1)
					align_queries<_val,_locr,_locl,1>(trace_pts, begin, end, *buffer, st);
				else if(query_contexts() == 2)
					align_queries<_val,_locr,_locl,2>(trace_pts, begin, end, *buffer, st);
				queue.push(i, Output_piece (buffer->get_begin(), buffer->size()));
			} catch(std::exception &e) {
				exception_state.set(e);
				queue.wake_all();
			}
		}
#pragma omp critical
		statistics += st;
	}
	exception_state.sync();
}

template<typename _val, typename _locr, typename _locl>
void align_queries(Trace_pt_list<_locr,_locl> &trace_pts, Output_stream* output_file)
{
	if(trace_pts.size() == 0)
		return;

	Output_writer writer (output_file);
	Task_queue3<Output_piece,Output_writer> queue (program_options::threads()*8, writer);

#pragma omp parallel
	{
		Statistics st;
		size_t i=0;
		typename Trace_pt_list<_locr,_locl>::Query_range query_range (trace_pts.get_range());
		while(queue.get(i, query_range) && !exception_state()) {
			try {
				Output_buffer<_val> *buffer = ref_header.n_blocks > 1 ? new Temp_output_buffer<_val> () : new Output_buffer<_val> ();
				if(query_contexts() == 6)
					align_queries<_val,_locr,_locl,6>(query_range.begin, query_range.end, *buffer, st);
				else if(query_contexts() == 1)
					align_queries<_val,_locr,_locl,1>(query_range.begin, query_range.end, *buffer, st);
				else if(query_contexts() == 2)
					align_queries<_val,_locr,_locl,2>(query_range.begin, query_range.end, *buffer, st);
				queue.push(i, Output_piece (buffer->get_begin(), buffer->size()));
			} catch(std::exception &e) {
				exception_state.set(e);
				queue.wake_all();
			}
		}
#pragma omp critical
		statistics += st;
	}
	exception_state.sync();
}

template<typename _val, typename _locr, typename _locl>
void align_queries(const Trace_pt_buffer<_locr,_locl> &trace_pts, Output_stream* output_file)
{
	Trace_pt_list<_locr,_locl> v;
	for(unsigned bin=0;bin<trace_pts.bins();++bin) {
		log_stream << "Processing query bin " << bin+1 << '/' << trace_pts.bins() << '\n';
		task_timer timer ("Loading trace points", false);
		trace_pts.load(v, bin);
		v.init();
		timer.go("Sorting trace points");
#if __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 3)
		__gnu_parallel::sort(v.begin(), v.end());
#else
		merge_sort(v.begin(), v.end(), program_options::threads());
#endif
		timer.go("Computing alignments");
		align_queries<_val,_locr,_locl>(v, output_file);
	}
}

#endif /* ALIGN_QUERIES_H_ */