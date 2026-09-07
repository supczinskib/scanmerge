// SPDX-License-Identifier: GPL-3.0-only

import Foundation

struct CheckFailure: Error { let message: String }
@MainActor func require(_ condition: Bool, _ message: String) throws {
    if !condition { throw CheckFailure(message: message) }
}
@main struct ControllerTests {
    @MainActor static func wait(_ model: ScanModel) async throws {
        let end = Date().addingTimeInterval(30)
        while model.running && Date() < end { try await Task.sleep(nanoseconds: 20_000_000) }
        try require(!model.running, "controller completion timeout")
    }
    @MainActor static func main() async throws {
        let fm = FileManager.default
        let env = ProcessInfo.processInfo.environment
        guard let rgePath = env["SCANMERGE_TEST_RGE"], let inputPath = env["SCANMERGE_TEST_INPUT"],
            fm.fileExists(atPath: rgePath), fm.fileExists(atPath: inputPath)
        else {
            throw CheckFailure(
                message: "Set SCANMERGE_TEST_RGE and SCANMERGE_TEST_INPUT to compatible scan data")
        }
        let root = URL(fileURLWithPath: fm.currentDirectoryPath)
        let temp = fm.temporaryDirectory.appendingPathComponent(
            "scanmerge-controller-" + UUID().uuidString)
        try fm.createDirectory(at: temp, withIntermediateDirectories: true)
        defer { try? fm.removeItem(at: temp) }
        let input = temp.appendingPathComponent("scans with spaces & symbols")
        try fm.createDirectory(at: input, withIntermediateDirectories: true)
        try fm.copyItem(
            at: URL(fileURLWithPath: rgePath),
            to: input.appendingPathComponent("view.rge"))
        let xml =
            "<PROJECT><SYSTEM><NAME>capture.fix_prj</NAME><SCANNER>SE2</SCANNER><TEX>false</TEX><FRAMES>1</FRAMES><POINTS>0</POINTS><GENERATE_CLOUD>0</GENERATE_CLOUD></SYSTEM><SCENE><PATH>./</PATH><DATA><MESH><NAME>view</NAME><ERR>-1</ERR><GROUP_NAME>Group1</GROUP_NAME></MESH></DATA></SCENE></PROJECT>"
        try xml.write(
            to: input.appendingPathComponent("capture.fix_prj"), atomically: true, encoding: .utf8)
        let engine = root.appendingPathComponent("dist/ScanMerge.app/Contents/MacOS/scanmerge")
        L10n.resources = Bundle(url: root.appendingPathComponent("dist/ScanMerge.app"))!
        let model = ScanModel(engineURL: engine)
        model.input = input
        model.outputParent = temp
        model.outputName = "output with spaces & symbols"
        model.exportPLY = true
        try require(model.canStart, "start enabled for valid inputs")
        model.start()
        try require(model.running, "asynchronous start")
        try await wait(model)
        try require(
            model.resultFolder != nil && model.errorMessage == nil, "successful real engine result")
        try require(
            fm.fileExists(atPath: model.resultFolder!.appendingPathComponent("merged.ply").path),
            "optional PLY")
        try require(
            model.stage == (L10n.language == "pl" ? "Gotowe" : "Complete"), "finished state")
        let resultFile = model.resultFolder!.appendingPathComponent("EXScanS/merged.sln_fix")
        let original = try Data(contentsOf: resultFile)
        // Reusing a nonempty output produces an error without replacing the result.
        model.start()
        try await wait(model)
        try require(
            model.errorMessage?.contains(L10n.language == "pl" ? "nie jest pusty" : "is not empty")
                == true, "friendly output collision error")
        let unchanged = try Data(contentsOf: resultFile)
        try require(unchanged == original, "existing result retained")
        model.input = URL(fileURLWithPath: inputPath)
        model.outputName = "cancelled"
        model.start()
        try await Task.sleep(nanoseconds: 100_000_000)
        model.cancel()
        try await wait(model)
        try require(
            model.stage == (L10n.language == "pl" ? "Anulowano" : "Cancelled"), "cancelled state")
        try require(
            !fm.fileExists(atPath: temp.appendingPathComponent("cancelled").path),
            "cancelled output not published")
        print(
            "PASS: real C++ engine through GUI controller; asynchronous completion; JSON progress; PLY; spaced paths; collision handling; cancellation. No application windows opened."
        )
    }
}
