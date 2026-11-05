// Lists the code that references each given address (function, instruction address, ref type).
// Run headless: ... -postScript RefsTo.java <out.txt> <hexAddr> [hexAddr...]
// @category JC2

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.symbol.Reference;

import java.io.FileWriter;
import java.io.PrintWriter;

public class RefsTo extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        try (PrintWriter out = new PrintWriter(new FileWriter(args[0]))) {
            for (int i = 1; i < args.length; i++) {
                Address target = toAddr(Long.parseLong(args[i], 16));
                out.println("== " + target);
                int n = 0;
                for (Reference r : getReferencesTo(target)) {
                    Address from = r.getFromAddress();
                    Function f = getFunctionContaining(from);
                    Instruction insn = getInstructionAt(from);
                    out.println("  " + from + "  " + (f == null ? "?" : f.getName()) + "  " + r.getReferenceType() + "  " +
                                (insn == null ? "" : insn.toString()));
                    if (++n >= 60) { out.println("  ..."); break; }
                }
            }
        }
    }
}
