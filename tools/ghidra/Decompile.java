// Decompiles the functions containing the given addresses and prints them, with callers.
// Run headless:
//   analyzeHeadless <projects folder> JC2 -process JustCause2.exe -noanalysis -readOnly
//       -scriptPath tools\ghidra -postScript Decompile.java <out.txt> <hexaddr> [<hexaddr> ...]
// @category JC2

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;

import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.HashSet;
import java.util.Set;

public class Decompile extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        try (PrintWriter out = new PrintWriter(new FileWriter(args[0]))) {
            DecompInterface decomp = new DecompInterface();
            decomp.openProgram(currentProgram);
            Set<Function> done = new HashSet<>();
            for (int i = 1; i < args.length; i++) {
                Address addr = toAddr(Long.parseLong(args[i], 16));
                Function f = getFunctionContaining(addr);
                if (f == null) {
                    out.println("// " + args[i] + ": no function");
                    continue;
                }
                out.println("// ===== " + args[i] + " is in " + f.getName() + " @ " + f.getEntryPoint());
                if (!done.add(f)) {
                    out.println("// (already printed)\n");
                    continue;
                }
                StringBuilder callers = new StringBuilder();
                for (Reference r : getReferencesTo(f.getEntryPoint())) {
                    Function c = getFunctionContaining(r.getFromAddress());
                    callers.append(" ").append(r.getFromAddress()).append(c != null ? "(" + c.getName() + ")" : "");
                }
                out.println("// callers:" + callers);
                DecompileResults res = decomp.decompileFunction(f, 60, monitor);
                out.println(res.decompileCompleted() ? res.getDecompiledFunction().getC() : "// decompile failed: " + res.getErrorMessage());
            }
        }
    }
}
