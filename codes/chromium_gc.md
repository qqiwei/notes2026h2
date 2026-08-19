# chromium GC 文献综述：并发、增量、并行、分代、分块  
  
  
  
  
## chromium partition alloc：时空局部性  
  
[todo] [https://chromium.googlesource.com/chromium/src/+/master/base/allocator/partition_allocator/PartitionAlloc.md](https://chromium.googlesource.com/chromium/src/+/master/base/allocator/partition_allocator/PartitionAlloc.md)  
  
标准内存分配器中的 free list 已有优化点（空间局部性）：  
- 大小对象分桶，加快匹配搜索到合适的空间  
- 切割&合并：没有完全匹配的空间，剩余部分挂回free list；回收阶段，可尝试与相邻空闲空间合并，这耗时，且无法根治碎片化  
  
Partition Alloc（缓存局部性）：在标准内存分配器之上的增强版  
- TLS cache  
- Partition pages 同样大小的对象分配到同一个区中  
  
OS（时间局部性）：支持归还虚拟地址空间但暂时不归还物理内存，以加速快速内存分配与回收  
  
## chromium 67 blink GC：STW 标记、清理  
  
[https://chromium.googlesource.com/experimental/chromium/src/+/refs/tags/67.0.3386.1/third_party/WebKit/Source/platform/heap/BlinkGCDesign.md](https://chromium.googlesource.com/experimental/chromium/src/+/refs/tags/67.0.3386.1/third_party/WebKit/Source/platform/heap/BlinkGCDesign.md)  
  
标记、清理（从2～4.4是STW的）：  
1. GC线程触发  
2. 等待mutator线程进入安全点（非安全点：扫描栈指针的保守标记）  
3. GC线程标记，顺便清理简单弱引用（很像Apple的弱引用，V8则用了三色标记法），这很快（O1）  
4. 清扫（各个mutator独立地懒惰清扫）  
    1. 不简单的弱引用清理推迟到了这个时候来执行，主要是因为要将弱引用从map中删掉这种回调函数执行成本高，均摊到各个线程中执行性能更好  
    2. pre-finalizers：析构函数之前，还能继续触碰到行将析构的堆对象。开销大，需要扫描所有pre-finalizer并执行行将就木对象的这些回调函数  
    3. 急迫的析构：能访问非急切析构的对象，保证mutator恢复前执行。但是棘手难用。  
    4. 恢复mutator执行  
    5. mutator分配新空间的时候，真正析构，进一步均摊析构  
  
  
## 三色标记法：并发标记  
  
[https://www.cnblogs.com/chanshuyi/p/head-first-of-triple-color-marking-algorithm.html](https://www.cnblogs.com/chanshuyi/p/head-first-of-triple-color-marking-algorithm.html)  
  
思考：本质上是一个广度优先搜索。标记的不变量是：保持已知，清理未知。目的是：最终清理不可知。  
  
mutate & GC 竞争：  
- 安全的 mutation：  
    - 未知 <—> 不可知（在这个不变量上，二者等价，合并）  
    - 已知 —> 未知/不可知  
        - 已知 —> 未知（等待扫描呗）  
        - 已知 —> 不可知（等待下次扫描呗）  
- 不安全的 mutation：原 已不可知（断开 唯一连接）—>后 又可知（重连）  
    - 未知/不可知 —> 已知（这次扫描完就被清理了，未能保持已知）  
  
竞争的两种解法：  
- 增量更新：在已知增加的时候，标记已知为待稍后扫描（已知一定是要保持的，故不存在误标）  
    - 性能问题场景：如果一个大已知的某个小部分频繁更新，会造成大已知所有的其他部分级联重新扫描  
- 初始快照：在未知/不可知减少的时候，标记未知/不可知为待稍后扫描  
    - 性能问题场景：在 未知/不可知 —> 不可知 场景下，这些不可知就要留待下次扫描解决，构成浮动垃圾  
  
思考：DFS vs BFS：对象成员关系具有局部性，BFS与局部性具有更好的适配度  
  
  
## chromium cppgc：概述  
  
[https://chromium.googlesource.com/v8/v8/+/main/include/cppgc/README.md](https://chromium.googlesource.com/v8/v8/+/main/include/cppgc/README.md)  
  
翻译：  
- 线程模型 —> TLS heap  
    - Member：用作GC对象成员；Persistent 用作非GC对象成员或静态对象。  
    - cppgc mutate 与 mutate 之间：使用 cross thread persistent 跨线程（但是坏味道，虽然弱引用版本能解决）。因而，只有单一方向的强引用，无循环引用（是根不是成员）。  
    - v8 与 blink 之间：仍然使用 Member（.gn 中开启 clang 编译选项 blink_gc_plugin 会校验 Member 只能指向 GarbageCollected 或 ScriptWrappable，后者是跨 v8 和 blink 的）。  
- 精确/保守 GC：真正STW、扫描运行栈的是保守的，因为可能碰巧将非堆指针的栈数据当做了存活的堆指针（安全点和保守栈扫描是栈上堆指针可以是原生指针的原因）  
- 原子、增量和并发 标记  
    - 原子：STW  
    - 增量：三色标记，mutate 在遇到内存屏障的时候完成一些标记留待重扫的操作  
        - 性能：内存屏障在赋值符号重载代码中插入，虽然静态编译入代码中，但大多数情况下，由于并不在GC中，不需要做标记留待重扫的操作，因而还是很快的  
    - 并发：mutate 不做，后台线程做   
- 标记、清理：见 chromium 67 blink GC   
  
  
## GC vs non-GC  
  
[https://en.wikipedia.org/wiki/Garbage_collection_(computer_science)#Tracing](https://en.wikipedia.org/wiki/Garbage_collection_(computer_science)#Tracing)  
  
思考：  
- GC vs OS non-GC  
    - Linux 内核：非抢占式，为整体吞吐量计  
    - Linux 用户线程：抢占式，内核有急迫的通知给用户层  
    - GC：协商式，没有比 mutate 线程更高的权限，只能发同步通知，等待mutate到达安全点  
- tracing GC vs ref-count  
    - 引用计数：主要是因为在大型项目中难以解决循环引用等复杂问题；至于简单赋值也都会引入耗性能等底层原子操作，内存屏障也一样（虽然大多数都是简单赋值），而且cpp引入了移动，可以解决很多赋值中非必要操作；每个引用计数都会占用必不可少的空间。  
    - 析构：不主用析构清理资源，而是依赖引用关系  
  
引用计数：  
- 优势：确定性、局部性  
- 优化：在其他GC方式中，也存在很多类似的措施  
    - 循环引用问题：弱引用  
    - 原子性开销：  
        - 合并更新（类似C++移动操作）  
        - 缓存更新并由后台线程批量处理  
        - 分线程计数（类似CPU核心缓存的线程版）  
    - 空间开销：使用64位中未使用的位存储较小的引用计数  
  
  
## chromium blink GC api：多继承  
  
[https://chromium.googlesource.com/chromium/src/+/master/third_party/blink/renderer/platform/heap/BlinkGCAPIReference.md](https://chromium.googlesource.com/chromium/src/+/master/third_party/blink/renderer/platform/heap/BlinkGCAPIReference.md)  
  
API（知识增量）：  
- 初始化绑定：用 AttachMainThread 或 AttachCurrentThread 绑定线程与GC（对主线程，绑定到全局对象上；对其他线程，TLS分配）  
- GarbageCollected 的多继承痛点：  
    - 左派生规则：因为this在非左一基类中是子孙类this加其左边对象大小后得到的，this在GC中将不再唯一（参见 深入理解C++对象模型）  
    - 非左基类：在GarbageCollectedMixin（USING_GARBAGE_COLLECTED_MIXIN 宏）子类中实现Trace，在子类中将偏移后的this传给它  
- 弱引用：GC能回收循环引用 ≠ 应该让对象一直活着 e.g. JS 为元素添加事件监听时，生成一个强引用元素的EventListener对象，为了让后者可能的被回收更快完成，元素的EventListener对象集合的类型就是弱引用类型的  
  
  
## generational GC：分代&标记跨代根  
  
[https://en.wikipedia.org/wiki/Tracing_garbage_collection#Generational_GC_(ephemeral_GC)](https://en.wikipedia.org/wiki/Tracing_garbage_collection#Generational_GC_(ephemeral_GC))  
  
弱（经验性观察结果）分代假说：90%的对象在前几次新生代扫描中就会死去。  
- 新生代扫描15次（比如说）依然存活就晋升到老年代了  
- 注：它没说活得越久的对象一定还会活得越久  
- 死得快 生得也快 假说：  
    - 新生代中又划分了伊甸园和两个生还区，占比：8:1:1；不采用压缩整理，缘于在两个生还区中乒乓的新生代对象极少，拷贝更快  
    - 且伊甸园中存在线程局部空间，以加速分配  
    - 要求：新生代GC快、不阻塞  
  
跨代引用问题：  
- 新生代<—老年代：老年代空间对应有卡表，对应的位表示一个老年代对象是否存在引用新生代对象，存在则需要在新生代GC中作为根  
    - 如果没有卡表，新生代GC需要扫描整个老年代对象作为根，很慢，失去了分代的意义  
        - p.s. 在新生代晋升的时候，可能需要更新卡表  
    - 新生代小（1/3或更小），扫描才会快；老年代大且变化少，卡表有意义，且不会被频繁更新 —> 在 g1 中新老代占比是动态的  
- 新生代—>老年代：在老年代GC中，以极少数新生代对象为根做扫描  
  
- 卡表：  
    - 结构：如果堆空间是 D，卡表数组就是 D / 512，每个字节表示内存里的512字节（卡，在CMS&G1中一致）  
        - 用字节不用位：CPU处理字节更快；一个字节里多个位同事修改会引发缓存一致性的竞争  
    - 卡表索引：对于一个对象的地址 Addr，它所在的卡在卡表中的索引就是：(Addr - HeapStart) >> 9  
    - 标脏：标记的是对象所在的整个卡在卡表中的位置：CARD_TABLE[address >> 9] = DIRTY;  
  
  
## JVM GC：G1：先行者&集大成者  
  
CMS 的问题：  
- G1：引入Region：从字节到块（包括freelist）：将内存分为～2K个大小同等的Region（1～32MB），动态扮演 Eden、Survivor、Old（或 Humongous）  
- 碎片问题：不移动对象，就不能解决碎片问题（经常在空间够的时候没地方放大对象）  
    - CMS：老年代里的某些对象就像钉子户一样，让内存变得稀碎  
    - G1：只清理值得回收的老年代Region，而不是全部内存，更高效  
        - 记录每个Region标记后的存活率：存活字节占比  
        - 只回收全部年轻代和部分老年代（mixed GC：当老年代（+大对象）占比超过比如说45%，启动）  
        - 大对象（超过Region一半大小）视同老年代，更高效  
        - 被清理的Region中存活的对象，移动到全新的Region中，提高内存连续性，更高效  
- STW标记/回收压力问题：  
    - CMS 增量标记（追踪活对象，但活对象本身有可能频繁变化，影响异步处理） —> G1 初始快照（保留未知性，可能产生浮动垃圾）  
    - CMS 回收范围：遍历全局卡表的脏卡 —> G1 回收范围：引用我的Region或卡  
        - G1记忆集：以空间换时间；每Region一个，记录谁引了我，记录Region间引用关系  
            - 三种粒度：顺序：稀疏高精度—>细粒度—>粗粒度；  
                - 被引少：从Region到卡数组的映射，记录引用我的对象所在的Region/卡  
                - 某个源Region引用我变多：这个Region下的卡数组，将变成卡位图（小卡表，而非CMS老年代全局位图）  
                - 多个源Region引用我变多：类似OS多级页表，一个小位图，只记录Region；GC 时必须扫描那个Region，会显著增加扫描时间  
        - 并发标记：  
            - G1记忆集处理较复杂，不宜mutator做，打包成异步任务，放入TLS脏卡队列，在后台做（背压大时，才在mutator做）  
            - 对象并发标记：全堆扫描对象的存活性 vs 记忆集的并发标记：在扫描结束后，确定扫描回收的范围  
  
G1：垃圾优先：在有限时间内尽量回收更多的垃圾  
- 高回收价值的老年代Region优先回收  
- 初始快照，先回收确定是垃圾的部分，留下未知，慢慢再纠正多标问题  
  
历史：引用计数（自扫门前雪；CMS：存在推广的难度&原子操作浪费） —> CMS（集中扫，吞吐量大） —> G1（分区扫）  
  
  
## 2016.4 v8 orinoco jank busters p2：新生代：并行标记跨代根  
  
[https://v8.dev/blog/orinoco](https://v8.dev/blog/orinoco)  
  
三点优化：  
- 新老串行(之前，跨代引用复杂) —> 并行（Page化）标记&移动  
- Young GC 并行标记：老年代对象特别多，为了更快速地找到所有活跃的根（解决跨代引用问题：老年代—>新生代），引入并行  
    - before：一个全局大数组，内含所有老年代指向新生代的指针。问题：大量重复；难以并发：要锁数组，且乱序影响缓存命中；  
    - after：分了page，每个老年代page有自己的几个位图桶，表示对应地址的对象存在引用新生代。优势：可并行；且优化了空间占用。  
- Old GC 优化：大对象&历史统计预测长寿的对象走预提升机制（直接归属为老年代），直接标记为黑色，大不了产生一点浮动垃圾，但减少了  
  
两个名词：关于解决碎片的；一般说法  
- 疏散Evacuation：一般指新生代在伊甸区和两个存活区之间乒乓地拷贝，少量存活的对象，快速；后面分块了就不区分了，都叫疏散  
- 压缩Compaction：一般指旧版GC中老年代原地娜动拷贝，慢  
  
  
## 2016.10 v8 optimizing memory consumption：增量标记、低内存模式  
  
[https://v8.dev/blog/optimizing-v8-memory](https://v8.dev/blog/optimizing-v8-memory)  
  
两个概念：  
- 增量标记：时间分片，跑一会JS，停下来跑一会标记，再跑一会JS  
- 并发标记：后台扫描线程与JS主线程并行  
  
- 遭遇相同的竞争：都依赖三色标记法、写屏障来解决写冲突  
- 合作：为什么又了并发，还需要增量呢？—— 在标记背压大的时候，引入增量标记能限流。  
  
JS 堆缩减：  
- 内存缩减模式：适配低端设备  
    - 余量 slack：更少，更早GC  
    - 余量：硬性限制：会导致增量标记不再仅被外包给后台线程，而是主线程也不再继续执行JS，而是并行参与到标记中； 此时也不再需要内存屏障，因为没有来mutate线程。此刻，内存压力被认为比流畅性更重要。  
    - 更积极地压缩：宁愿花时间整理内存页  
- V8堆页减少（适用于所有机型）：减少内存占用与碎片；提高压缩线程并行度  
  
堆外内存：  
- Code Range：JIT 编译生成的机器码与AST、字节码  
- ArrayBuffer / SharedArrayBuffer：堆外的原始字节流，如图像、文件内容、网络流  
    - JS类型：一个 ArrayBuffer 可以同时被多个不同的 TypedArray 引用。  
- Global Handles：外部（C++）持有JS对象时登机用，作为GC根。  
- Isolate Data：V8实例运行状态机  
  
  
## 2017.11 v8 orinoco young GC：新生代：并行标记、疏散  
  
[https://v8.dev/blog/orinoco-parallel-scavenger](https://v8.dev/blog/orinoco-parallel-scavenger)  
  
v6.2之前：单线程 Cheney 的半空间复制  
- 扫描根(包括新生代和老生代），并将可达对象拷贝到另一半空间（alloc之后），直到源空间只剩下垃圾  
- 从scan到alloc之间的队列：待扫描，构成一个BFS队列（在Cheney发明半空间复制之前，DFS遍历对象图，会消耗大量栈空间）  
  
并行标记、疏散  
- 依然采用Cheney半空间复制解决碎片问题  
- 并行疏散：拷贝from空间的活对象，在复制结束后，归还剩余空间给主空间；以及，引入任务窃取  
- locksteps：  
    - 并行标记：旧：随时标记随搬运导致写入目标空间的竞争，新：并行标记仅通过原子操作在部分(分页、位图桶）from空间的位图中标记  
    - 并行疏散：多线程：从to空间批发小块空间缓存到本地，从from空间拷贝活对象，并在旧位置留下转发地址（使能并发扫描、疏散、指针更新）  
    - 并行指针更新：只能在所有活对象搬迁到新地址后，才能并发更新  
  
  
## 2018.6 v8 concurrent marking：老年代：并发标记  
  
[https://v8.dev/blog/concurrent-marking](https://v8.dev/blog/concurrent-marking)  
  
没有新鲜内容。请回顾。  
  
  
## 2019.1 v8 orinoco gc：总结  
  
[https://v8.dev/blog/trash-talk](https://v8.dev/blog/trash-talk)  
  
哲学：  
- 并行：主线程停JS，和辅助线程一起干  
- 增量：一会GC，一会JS  
- 并发：主线程JS，辅助线程GC  
  
老年代GC：  
- 并发标记：标记大部分在后台  
- 并行清除/压缩  
  
idle time GC：V8问Embedder忙不忙，如果Embedder在阻塞V8就可以做清理工作  
  
坦诚：写屏障造成5~10%的性能损耗。但是，宁愿慢，也不要卡顿。  
  
跨代引用（Page机制）：分块、局部化、使能并行化  
  
补充：MinorMS：新生代也不再使用并行疏散，而是采用原地回收，参考JVM G1  
  
  
## 2020.5 v8 high-perf cppgc：blink GC 回顾  
  
[https://v8.dev/blog/high-performance-cpp-gc](https://v8.dev/blog/high-performance-cpp-gc)  
  
V8将JS上积累的并发回收经验应用到C++对象回收上。  
  
走马观花：  
- 标记：访问者模式，C++ 对象通过继承 GarbageCollected 类并实现 Trace 方法，告诉回收器它引用了哪些其他对象。  
- 保守栈扫描：  
    - Oilpan 并不强迫开发者在栈上使用复杂的智能指针，而是通过逐字扫描执行栈（Stack），将看起来像指针的数值视为根。  
    - 为了降低成本，它会尽量在栈比较“干净”（即没有太多活跃对象引用）的任务间隙触发 GC。  
- 析构函数可能不是线程安全的  
    - 如果对象有析构函数，会延迟到主线程执行析构函数  
    - 如果没有，直接原地回收  
  
  
  
  
  
  
  
  
  
  
  
  
  
  
  
  
  
  
  
  
  
  
  
  
  
  
  
