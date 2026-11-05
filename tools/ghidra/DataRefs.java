// Lists the global data addresses (>= minData) that functions starting in [start, end) read or
// write, with the number of referencing functions and a few of their names.
// Run headless: ... -postScript DataRefs.java <out.txt> <startHex> <endHex> [minDataHex]
// @category JC2

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.symbol.Reference;

import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;

public class DataRefs extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        long start = Long.parseLong(args[1], 16), end = Long.parseLong(args[2], 16);
        long minData = args.length > 3 ? Long.parseLong(args[3], 16) : 0x01000000L;
        Map<Long, List<String>> refs = new TreeMap<>();
        Map<Long, String> kinds = new TreeMap<>();
        for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
            long entry = f.getEntryPoint().getOffset();
            if (entry < start || entry >= end) continue;
            InstructionIterator it = currentProgram.getListing().getInstructions(f.getBody(), true);
            while (it.hasNext()) {
                Instruction ins = it.next();
                for (Reference r : ins.getReferencesFrom()) {
                    Address to = r.getToAddress();
                    if (!to.isMemoryAddress()) continue;
                    long a = to.getOffset();
                    if (a < minData) continue;
                    List<String> l = refs.computeIfAbsent(a, k -> new ArrayList<>());
                    if (!l.contains(f.getName())) l.add(f.getName());
                    String k = r.getReferenceType().isWrite() ? "W" : "R";
                    String old = kinds.get(a);
                    kinds.put(a, old == null ? k : (old.contains(k) ? old : "RW"));
                }
            }
        }
        try (PrintWriter out = new PrintWriter(new FileWriter(args[0]))) {
            for (Map.Entry<Long, List<String>> e : refs.entrySet()) {
                List<String> l = e.getValue();
                out.printf("%08x %-2s %3d %s%n", e.getKey(), kinds.get(e.getKey()), l.size(),
                           String.join(" ", l.subList(0, Math.min(4, l.size()))));
            }
        }
    }
}
