// Decompiles the functions at module offsets (main.bin at base 0x8504000), appending them to a file; with
// ImportSymbols.java run first, calls and globals show by name. Headless: -postScript Decomp.java <out.c> <offset>...
//@category LASAP
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import java.io.FileWriter;
import java.io.PrintWriter;

public class Decomp extends GhidraScript {
    static final long BASE = 0x8504000L;

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        try (PrintWriter out = new PrintWriter(new FileWriter(args[0], true))) {
            DecompInterface decompiler = new DecompInterface();
            decompiler.openProgram(currentProgram);
            for (int i = 1; i < args.length; i++) {
                Address address = toAddr(BASE + Long.decode(args[i]));
                new DisassembleCommand(address, null, true).applyTo(currentProgram, monitor);
                Function function = getFunctionAt(address);
                if (function == null) function = createFunction(address, null);
                if (function == null) {
                    out.println("// no function at " + args[i]);
                    continue;
                }
                DecompileResults result = decompiler.decompileFunction(function, 120, monitor);
                out.println("// ===== " + args[i] + " " + function.getName());
                out.println(result.getDecompiledFunction() == null ? "// " + result.getErrorMessage()
                                                                   : result.getDecompiledFunction().getC());
            }
        }
    }
}
