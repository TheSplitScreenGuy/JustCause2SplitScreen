// Walks the call tree from root functions (callees inside [start, end) only, up to a depth) and
// lists the functions reached and the global data addresses (>= minData) they read or write.
// Run headless: ... -postScript TreeRefs.java <out.txt> <startHex> <endHex> <minDataHex> <depth> <root> [root...]
// @category JC2

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.symbol.Reference;

import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.*;

public class TreeRefs extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        long start = Long.parseLong(args[1], 16), end = Long.parseLong(args[2], 16);
        long minData = Long.parseLong(args[3], 16);
        int maxDepth = Integer.parseInt(args[4]);
        Map<Function, Integer> depth = new LinkedHashMap<>();
        Deque<Function> queue = new ArrayDeque<>();
        for (int i = 5; i < args.length; i++) {
            Function f = getFunctionContaining(toAddr(Long.parseLong(args[i], 16)));
            if (f != null && !depth.containsKey(f)) { depth.put(f, 0); queue.add(f); }
        }
        Map<Long, Set<String>> refs = new TreeMap<>();
        Map<Long, String> kinds = new HashMap<>();
        while (!queue.isEmpty()) {
            Function f = queue.poll();
            int d = depth.get(f);
            InstructionIterator it = currentProgram.getListing().getInstructions(f.getBody(), true);
            while (it.hasNext()) {
                Instruction ins = it.next();
                for (Reference r : ins.getReferencesFrom()) {
                    Address to = r.getToAddress();
                    if (!to.isMemoryAddress()) continue;
                    long a = to.getOffset();
                    if (r.getReferenceType().isCall()) {
                        Function c = getFunctionAt(to);
                        if (c != null && a >= start && a < end && d < maxDepth && !depth.containsKey(c)) {
                            depth.put(c, d + 1);
                            queue.add(c);
                        }
                        continue;
                    }
                    if (a < minData) continue;
                    refs.computeIfAbsent(a, k -> new LinkedHashSet<>()).add(f.getName());
                    String k = r.getReferenceType().isWrite() ? "W" : "R";
                    String old = kinds.get(a);
                    kinds.put(a, old == null ? k : (old.contains(k) ? old : "RW"));
                }
            }
        }
        try (PrintWriter out = new PrintWriter(new FileWriter(args[0]))) {
            out.println("# functions: " + depth.size());
            for (Map.Entry<Function, Integer> e : depth.entrySet())
                out.println("# " + e.getValue() + " " + e.getKey().getName());
            for (Map.Entry<Long, Set<String>> e : refs.entrySet()) {
                List<String> l = new ArrayList<>(e.getValue());
                out.printf("%08x %-2s %3d %s%n", e.getKey(), kinds.get(e.getKey()), l.size(),
                           String.join(" ", l.subList(0, Math.min(4, l.size()))));
            }
        }
    }
}
