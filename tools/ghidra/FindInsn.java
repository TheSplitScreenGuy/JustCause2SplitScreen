// Lists instructions whose text matches a regex (e.g. "MOV dword ptr \[\w+ \+ 0x46c\],"), with their function.
// Run headless: ... -postScript FindInsn.java <out.txt> <regex | @regexfile> [maxHits]
// @category JC2

import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;

import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.regex.Pattern;

public class FindInsn extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        // "@file" reads the regex from a file (the .bat launcher mangles |, ^ and spaces).
        String regex = args[1].startsWith("@")
            ? new String(java.nio.file.Files.readAllBytes(java.nio.file.Paths.get(args[1].substring(1)))).trim()
            : args[1];
        Pattern p = Pattern.compile(regex, Pattern.CASE_INSENSITIVE);
        int max = args.length > 2 ? Integer.parseInt(args[2]) : 200;
        int hits = 0;
        try (PrintWriter out = new PrintWriter(new FileWriter(args[0]))) {
            InstructionIterator it = currentProgram.getListing().getInstructions(true);
            while (it.hasNext() && hits < max) {
                Instruction ins = it.next();
                String text = ins.toString();
                if (p.matcher(text).find()) {
                    Function f = getFunctionContaining(ins.getAddress());
                    out.println(ins.getAddress() + "  " + text + "    ; " + (f != null ? f.getName() : "?"));
                    hits++;
                }
            }
        }
    }
}
