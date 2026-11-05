// Prints the disassembly of the functions containing the given addresses (or <count>
// instructions from an address given as addr:count).
// Run headless like Decompile.java: ... -postScript Disasm.java <out.txt> <hexaddr>[:count] ...
// @category JC2

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;

import java.io.FileWriter;
import java.io.PrintWriter;

public class Disasm extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        try (PrintWriter out = new PrintWriter(new FileWriter(args[0]))) {
            for (int i = 1; i < args.length; i++) {
                String[] parts = args[i].split(":");
                Address addr = toAddr(Long.parseLong(parts[0], 16));
                Instruction ins;
                Address end = null;
                int count = Integer.MAX_VALUE;
                if (parts.length > 1) {
                    count = Integer.parseInt(parts[1]);
                    ins = getInstructionAt(addr);
                    out.println("// ===== " + parts[0] + " (" + count + " instructions)");
                } else {
                    Function f = getFunctionContaining(addr);
                    if (f == null) { out.println("// " + parts[0] + ": no function"); continue; }
                    ins = getInstructionAt(f.getEntryPoint());
                    end = f.getBody().getMaxAddress();
                    out.println("// ===== " + f.getName() + " @ " + f.getEntryPoint());
                }
                for (int n = 0; ins != null && n < count; n++) {
                    if (end != null && ins.getAddress().compareTo(end) > 0) break;
                    out.println(ins.getAddress() + "  " + ins);
                    ins = ins.getNext();
                }
            }
        }
    }
}
