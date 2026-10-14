// Names functions and labels from a .sym file (scripts/re/las.sym): one symbol per line, "<name> <address> <f|l>
// [note]", the address absolute (main.bin at base 0x8504000), f a function (created if missing), l a label; the
// note becomes the function's or the address's comment. The format of Ghidra's former ImportSymbolsScript.py.
// Script Manager: asks for the file. Headless: -preScript ImportSymbols.java <file>.
//@category LASAP
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.CodeUnit;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.SourceType;
import java.io.File;
import java.nio.file.Files;

public class ImportSymbols extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        File file = args.length > 0 ? new File(args[0]) : askFile("Symbols (.sym)", "Import");
        int functions = 0, labels = 0;
        for (String line : Files.readAllLines(file.toPath())) {
            String[] parts = line.trim().split("\\s+", 4);
            if (parts.length < 2 || parts[0].startsWith("#")) continue;
            Address address = toAddr(Long.decode(parts[1]));
            String note = parts.length > 3 ? parts[3] : null;
            if (parts.length > 2 && parts[2].equals("f")) {
                Function function = getFunctionAt(address);
                if (function == null) {
                    new DisassembleCommand(address, null, true).applyTo(currentProgram, monitor);
                    function = createFunction(address, parts[0]);
                }
                if (function == null) {
                    printerr("no function at " + parts[1] + " (" + parts[0] + ")");
                    continue;
                }
                function.setName(parts[0], SourceType.USER_DEFINED);
                if (note != null) function.setComment(note);
                functions++;
            } else {
                createLabel(address, parts[0], true, SourceType.USER_DEFINED);
                if (note != null) currentProgram.getListing().setComment(address, CodeUnit.EOL_COMMENT, note);
                labels++;
            }
        }
        println("ImportSymbols: " + functions + " functions, " + labels + " labels from " + file.getName());
    }
}
