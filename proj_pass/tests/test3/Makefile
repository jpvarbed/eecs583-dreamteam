-include ../Makefile.defaults

default:
	 make clean
	 g++ correct.c -o correct_correct
	 ./correct_correct > output_correct
	 llvm-gcc -emit-llvm correct.c -c -o correct.bc
	 opt -loop-simplify correct.bc -o correct.ls.bc
	 llc correct.ls.bc -o correct.ls.s
	 g++ -o correct.ls correct.ls.s
	 ./correct.ls > output_correct_ls
	 opt -mem2reg correct.ls.bc -o correct.ls.mem2reg.bc
	 llc correct.ls.mem2reg.bc -o correct.ls.mem2reg.s
	 g++ -o correct.ls.mem2reg correct.ls.mem2reg.s
	 opt -insert-edge-profiling correct.ls.bc -o correct.profile.bc
	 llc correct.profile.bc -o correct.profile.s
	 g++ -o correct.profile correct.profile.s ${LLVMDIR}/Release/lib/profile_rt.so
	 -./correct.profile > output_profile
	 opt -load ${PROJDIR}/Debug+Asserts/lib/projpass.so -lamp-insts -profile-loader -profile-info-file=llvmprof.out -insert-lamp-profiling -insert-lamp-loop-profiling -insert-lamp-init < correct.ls.bc > correct.lamp.bc
	 llc correct.lamp.bc -o correct.lamp.s
	 g++ -o correct.lamp correct.lamp.s ${LLVMDIR}/Release/lib/profile_rt.so ${PROJDIR}/tools/lamp-profiler/lamp_hooks.o ${PROJDIR}/tools/lamp-profiler/loadstride.cxx
	 -./correct.lamp > output
	 opt -load ${PROJDIR}/Debug+Asserts/lib/projpass.so -lamp-inst-cnt -lamp-map-loop -lamp-load-profile -profile-loader -profile-info-file=llvmprof.out -projpass correct.ls.bc > correct.prefetch.bc
	llc correct.prefetch.bc -o correct.prefetch.s
	llvm-dis correct.prefetch.bc -o correct.prefetch.ll
	g++ -o correct.prefetch correct.prefetch.s
	opt -dot-cfg correct.prefetch.bc >& /dev/null
	mv cfg.main.dot cfg.prefetch.dot
	opt -dot-cfg correct.bc >& /dev/null
	mv cfg.main.dot cfg.correct.dot
	rm -f cfg.main.dot
	opt -mem2reg correct.prefetch.bc -o correct.prefetch.mem2reg.bc
	llc correct.prefetch.mem2reg.bc -o correct.prefetch.mem2reg.s
	llvm-dis correct.prefetch.mem2reg.bc -o correct.prefetch.mem2reg.ll
	g++ -o correct.prefetch.mem2reg correct.prefetch.mem2reg.s
	opt -dot-cfg correct.prefetch.mem2reg.bc >& /dev/null
	mv cfg.main.dot cfg.prefetch.mem2reg.dot
	rm -f cfg.main.dot

	-./correct.prefetch.mem2reg > output_prefetch
	-./correct.ls.mem2reg > output_correct_ls_mem2reg
	tail output_correct_ls_mem2reg
	tail output_prefetch
	
clean:
	rm -f llvmprof.out
	rm -f correct_correct output_correct
	rm -f result.lamp*
	rm -f correct.bc correct.opt*
	rm -f output output_prefetch correct.lamp* correct.ls* correct.profile* output_*
	rm -f correct.prefetch*
	rm -f cfg.*

#	 opt -loop-simplify < simple.bc > simple.ls.bc
#	 opt -load /y/students/mviscomi/hw1/Debug+Asserts/lib/sample.so -lamp-insts -insert-lamp-profiling -insert-lamp-loop-profiling -insert-lamp-init < simple.ls.bc > simple.lamp.bc
#	 llc < simple.lamp.bc > simple.lamp.s
#	 g++ -o simple.lamp.exe simple.lamp.s /y/students/mviscomi/hw1/tools/lamp-profiler/lamp_hooks.o
#	 -./simple.lamp.exe > output.lamp
