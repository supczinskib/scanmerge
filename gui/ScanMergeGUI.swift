// SPDX-License-Identifier: GPL-3.0-only

import AppKit
import SwiftUI

private final class ErrorBuffer: @unchecked Sendable {
    private let lock = NSLock()
    private var data = Data()
    func append(_ chunk: Data) {
        lock.lock()
        defer { lock.unlock() }
        data.append(chunk)
        if data.count > 65536 { data.removeFirst(data.count - 65536) }
    }
    var text: String {
        lock.lock()
        defer { lock.unlock() }
        return String(decoding: data, as: UTF8.self)
    }
}

@MainActor final class ScanModel: ObservableObject {
    private let configuredEngine: URL?
    init(engineURL: URL? = nil) { configuredEngine = engineURL }
    @Published var input: URL?
    @Published var outputParent: URL?
    @Published var outputName = L10n.text("ScanMerge-output")
    @Published var exportPLY = true
    @Published var running = false
    @Published var cancelling = false
    @Published var stage = L10n.text("Ready")
    @Published var detail = L10n.text("Select a directory containing EXScanS scans.")
    @Published var fraction: Double?
    @Published var resultFolder: URL?
    @Published var resultDescription = ""
    @Published var errorMessage: String?
    @Published var terminalMessage = ""
    @Published var terminalEnabled = false
    @Published var terminalManaged = false
    @Published var elapsed: TimeInterval = 0
    private var process: Process?
    private var started: Date?
    private var timer: Timer?
    private var resultEvent: [String: Any]?
    private var lineBuffer = Data()
    var afterFinish: (() -> Void)?

    var executable: URL {
        configuredEngine ?? Bundle.main.bundleURL.appendingPathComponent("Contents/MacOS/scanmerge")
    }
    var terminal: TerminalIntegration {
        let env = ProcessInfo.processInfo.environment
        return TerminalIntegration(
            home: FileManager.default.homeDirectoryForCurrentUser, executable: executable,
            shell: env["SHELL"] ?? "/bin/zsh",
            zDotDir: env["ZDOTDIR"].map { URL(fileURLWithPath: $0) })
    }
    var output: URL? { outputParent?.appendingPathComponent(outputName, isDirectory: true) }
    var canStart: Bool {
        input != nil && outputParent != nil
            && !outputName.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty
            && outputName != "." && outputName != ".." && !outputName.contains("/")
            && !outputName.contains(":") && !running
    }
    func refreshTerminal() {
        terminalEnabled = terminal.isEnabled
        terminalManaged = terminal.isManaged
    }
    func pickInput() {
        let panel = NSOpenPanel()
        panel.canChooseDirectories = true
        panel.canChooseFiles = false
        panel.allowsMultipleSelection = false
        panel.prompt = L10n.text("Select scans")
        panel.message = L10n.text(
            "Select the directory containing an EXScanS project and its captures.")
        if panel.runModal() == .OK, let url = panel.url {
            input = url
            if outputParent == nil { outputParent = url.deletingLastPathComponent() }
            resultFolder = nil
            errorMessage = nil
        }
    }
    func pickOutput() {
        let panel = NSOpenPanel()
        panel.canChooseDirectories = true
        panel.canChooseFiles = false
        panel.allowsMultipleSelection = false
        panel.canCreateDirectories = true
        panel.prompt = L10n.text("Select destination")
        panel.message = L10n.text("ScanMerge will create a separate output directory here.")
        panel.directoryURL = outputParent
        if panel.runModal() == .OK {
            outputParent = panel.url
            resultFolder = nil
            errorMessage = nil
        }
    }
    func toggleTerminal(enable: Bool) {
        do { terminalMessage = try enable ? terminal.enable() : terminal.disable() } catch {
            terminalMessage = error.localizedDescription
        }
        refreshTerminal()
    }
    func start() {
        guard canStart, let input, let output else { return }
        errorMessage = nil
        resultFolder = nil
        resultEvent = nil
        lineBuffer.removeAll()
        do {
            let p = Process()
            p.executableURL = executable
            p.arguments =
                [input.path, output.path] + (exportPLY ? ["--ply"] : []) + ["--json-progress"]
            let stdout = Pipe()
            let stderr = Pipe()
            p.standardOutput = stdout
            p.standardError = stderr
            try p.run()
            process = p
            running = true
            cancelling = false
            elapsed = 0
            started = Date()
            stage = L10n.text("Starting")
            detail = L10n.text("Checking the project and EXScanS installation.")
            fraction = nil
            timer = Timer.scheduledTimer(withTimeInterval: 0.25, repeats: true) { [weak self] _ in
                Task { @MainActor in
                    if let start = self?.started { self?.elapsed = Date().timeIntervalSince(start) }
                }
            }
            let errors = ErrorBuffer()
            let readers = DispatchGroup()
            readers.enter()
            DispatchQueue.global(qos: .userInitiated).async { [weak self] in
                while true {
                    let data = stdout.fileHandleForReading.availableData
                    if data.isEmpty { break }
                    DispatchQueue.main.async { self?.receive(data) }
                }
                readers.leave()
            }
            readers.enter()
            DispatchQueue.global(qos: .utility).async {
                while true {
                    let data = stderr.fileHandleForReading.availableData
                    if data.isEmpty { break }
                    errors.append(data)
                }
                readers.leave()
            }
            DispatchQueue.global(qos: .userInitiated).async { [weak self] in
                p.waitUntilExit()
                readers.wait()
                let status = p.terminationStatus
                let text = errors.text
                DispatchQueue.main.async {
                    self?.finished(status: status, errors: text, output: output)
                }
            }
        } catch {
            errorMessage = L10n.text("Could not start the engine. %@", error.localizedDescription)
        }
    }
    private func receive(_ data: Data) {
        lineBuffer.append(data)
        while let end = lineBuffer.firstIndex(of: 10) {
            let line = Data(lineBuffer[..<end])
            lineBuffer.removeSubrange(...end)
            guard let event = (try? JSONSerialization.jsonObject(with: line)) as? [String: Any]
            else { continue }
            if event["event"] as? String == "result" {
                resultEvent = event
                continue
            }
            guard !cancelling else { continue }
            let name = event["stage"] as? String ?? ""
            let done = event["completed"] as? Int ?? 0
            let total = event["total"] as? Int ?? 0
            fraction = total > 0 ? min(1, Double(done) / Double(total)) : nil
            switch name {
            case "validate":
                stage = L10n.text("Checking project")
                detail = L10n.text("Verifying project files and the EXScanS library.")
            case "read":
                stage = L10n.text("Reading captures")
                detail = L10n.text("Capture %ld of %ld", done, total)
            case "prepare":
                stage = L10n.text("Preparing registration")
                detail = L10n.text("Group %ld of %ld", done, total)
            case "alignment":
                stage = L10n.text("Aligning groups")
                detail =
                    total > 0
                    ? L10n.text("Aligned %ld of %ld groups", done, total)
                    : L10n.text("Finding overlapping surfaces.")
            case "refinement":
                stage = L10n.text("Refining alignment")
                detail =
                    total > 0
                    ? L10n.text("Iteration %ld of %ld", done, total)
                    : L10n.text("Checking connections between captures.")
            case "export":
                stage = L10n.text("Saving and verifying output")
                detail = L10n.text("Capture %ld of %ld", done, total)
            case "publish":
                stage = L10n.text("Finishing export")
                detail = L10n.text("All points have been saved and verified.")
            default: break
            }
        }
    }
    private func finished(status: Int32, errors: String, output: URL) {
        timer?.invalidate()
        timer = nil
        running = false
        process = nil
        if status == 0, let event = resultEvent,
            let path = event["project"] as? String, FileManager.default.fileExists(atPath: path)
        {
            stage = L10n.text("Complete")
            detail = L10n.text("Output saved. Source scans are unchanged.")
            fraction = 1
            resultFolder = output
            let count = event["points"] as? Int ?? 0
            let seconds = event["seconds"] as? Double ?? elapsed
            resultDescription = L10n.text(
                "%@ points · %ld s", count.formatted(.number.locale(L10n.locale)),
                Int(seconds.rounded()))
        } else if cancelling || status == 130 {
            stage = L10n.text("Cancelled")
            detail = L10n.text("No partial project was saved.")
            fraction = nil
        } else {
            stage = L10n.text("Alignment failed")
            detail = L10n.text("Source scans are unchanged.")
            fraction = nil
            errorMessage = friendlyError(errors)
        }
        cancelling = false
        let callback = afterFinish
        afterFinish = nil
        callback?()
    }
    private func friendlyError(_ text: String) -> String {
        if text.contains("Output directory must") {
            return L10n.text(
                "The output directory is not empty. Choose another name; existing files will not be overwritten."
            )
        }
        if text.contains("directories must be separate") {
            return L10n.text("The output directory must be outside the input directory.")
        }
        if text.contains("Unsupported EXScanS") {
            return L10n.text(
                "This EXScanS build is not supported. ScanMerge requires the validated 3.2.0.4 build for Apple Silicon."
            )
        }
        if text.contains("EXScanS is required") {
            return L10n.text(
                "EXScanS was not found in Applications. Install EXScanS to read and write its projects."
            )
        }
        if text.contains("No .sln_fix") {
            return L10n.text(
                "No EXScanS project (.sln_fix or .fix_prj) was found in the selected directory.")
        }
        if text.contains("More than one solution") {
            return L10n.text(
                "This directory contains multiple .sln_fix solutions. Select a directory with one solution and its files."
            )
        }
        if text.contains("Disconnected") || text.contains("No reliable connection") {
            return L10n.text(
                "Could not reliably align all captures. Check that they show the same object and contain overlapping surfaces."
            )
        }
        return text.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty
            ? L10n.text("The engine exited without a complete result.")
            : text.trimmingCharacters(in: .whitespacesAndNewlines)
    }
    func cancel() {
        guard running, !cancelling else { return }
        cancelling = true
        stage = L10n.text("Cancelling")
        detail = L10n.text("Finishing the current operation and removing temporary files.")
        process?.interrupt()
    }
}

struct FolderRow: View {
    let title: String
    let path: URL?
    let placeholder: String
    let action: () -> Void
    var body: some View {
        HStack(spacing: 14) {
            Image(systemName: "folder").font(.title2).foregroundStyle(.secondary).frame(width: 28)
            VStack(alignment: .leading, spacing: 4) {
                Text(title).font(.headline)
                Text(path?.path ?? placeholder).font(.callout).foregroundStyle(.secondary)
                    .lineLimit(2).truncationMode(.middle).textSelection(.enabled)
            }
            Spacer(minLength: 8)
            Button(L10n.text("Choose…"), action: action)
        }.padding(14).background(.background, in: RoundedRectangle(cornerRadius: 10))
    }
}

struct MainView: View {
    @ObservedObject var model: ScanModel
    @State private var terminalExpanded = false
    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 18) {
                HStack(spacing: 12) {
                    Image(
                        nsImage: NSImage(
                            contentsOf: Bundle.main.bundleURL.appendingPathComponent(
                                "Contents/Resources/AppIcon.png")) ?? NSImage()
                    )
                    .resizable().scaledToFit().frame(width: 52, height: 52)
                    VStack(alignment: .leading, spacing: 4) {
                        Text("ScanMerge").font(.largeTitle.weight(.semibold))
                        Text(L10n.text("Align captures. Preserve every point.")).foregroundStyle(
                            .secondary)
                    }
                    Spacer()
                    Text("Apple Silicon").font(.caption).foregroundStyle(.secondary)
                }
                VStack(spacing: 10) {
                    FolderRow(
                        title: L10n.text("Input scans"), path: model.input,
                        placeholder: L10n.text("EXScanS project directory"), action: model.pickInput
                    )
                    VStack(spacing: 10) {
                        FolderRow(
                            title: L10n.text("Output location"), path: model.outputParent,
                            placeholder: L10n.text("A new output directory will be created here"),
                            action: model.pickOutput)
                        HStack {
                            Text(L10n.text("Directory name:")).foregroundStyle(.secondary)
                            TextField(L10n.text("ScanMerge-output"), text: $model.outputName)
                                .textFieldStyle(.roundedBorder)
                        }.padding(.horizontal, 14).padding(.bottom, 12)
                    }.background(.background, in: RoundedRectangle(cornerRadius: 10))
                    Toggle(
                        L10n.text("Also export a merged PLY point cloud"), isOn: $model.exportPLY
                    )
                    .frame(maxWidth: .infinity, alignment: .leading).padding(.horizontal, 4)
                    Text(
                        L10n.text(
                            "An EXScanS project is always saved. Open the optional PLY in CloudCompare or another PLY reader."
                        )
                    )
                    .font(.caption).foregroundStyle(.secondary).frame(
                        maxWidth: .infinity, alignment: .leading
                    ).padding(.horizontal, 4)
                }.disabled(model.running)
                VStack(alignment: .leading, spacing: 10) {
                    HStack {
                        Text(model.stage).font(.headline)
                        Spacer()
                        if model.running {
                            Text("\(Int(model.elapsed)) s").monospacedDigit().foregroundStyle(
                                .secondary)
                        } else if model.resultFolder != nil {
                            Text(model.resultDescription).foregroundStyle(.secondary).font(.callout)
                        }
                    }
                    if model.running {
                        if let fraction = model.fraction {
                            ProgressView(value: fraction)
                        } else {
                            ProgressView().controlSize(.small)
                        }
                    }
                    Text(model.detail).font(.callout).foregroundStyle(.secondary)
                    if let error = model.errorMessage {
                        Text(error).font(.callout).foregroundStyle(.red).textSelection(.enabled)
                    }
                    HStack {
                        if let folder = model.resultFolder {
                            Button(L10n.text("Show output in Finder")) {
                                NSWorkspace.shared.open(folder)
                            }
                        }
                        Spacer()
                        if model.running {
                            Button(
                                model.cancelling ? L10n.text("Cancelling…") : L10n.text("Cancel"),
                                action: model.cancel
                            ).disabled(model.cancelling)
                        } else {
                            Button(L10n.text("Align scans"), action: model.start).buttonStyle(
                                .borderedProminent
                            )
                            .controlSize(.large).disabled(!model.canStart).keyboardShortcut(
                                .defaultAction)
                        }
                    }
                }.padding(16).background(.background, in: RoundedRectangle(cornerRadius: 10))
                DisclosureGroup(isExpanded: $terminalExpanded) {
                    VStack(alignment: .leading, spacing: 10) {
                        Text(L10n.text("Optionally run the same engine from Terminal:")).font(
                            .callout)
                        Text("scanmerge \"/input\" \"/output\" --ply").font(
                            .system(.callout, design: .monospaced)
                        ).textSelection(.enabled)
                        Text(
                            L10n.text(
                                "Adds scanmerge to your Terminal commands. Refresh the command after moving the application."
                            )
                        )
                        .font(.caption).foregroundStyle(.secondary)
                        HStack {
                            Button(
                                model.terminalEnabled
                                    ? L10n.text("Refresh Terminal command")
                                    : L10n.text("Enable Terminal command")
                            ) { model.toggleTerminal(enable: true) }
                            if model.terminalManaged {
                                Button(L10n.text("Disable Terminal command")) {
                                    model.toggleTerminal(enable: false)
                                }
                            }
                        }.disabled(model.running)
                        if !model.terminalMessage.isEmpty {
                            Text(model.terminalMessage).font(.callout).textSelection(.enabled)
                        }
                    }.padding(.top, 10)
                } label: {
                    Label(
                        model.terminalEnabled
                            ? L10n.text("Terminal · command enabled")
                            : L10n.text("Terminal integration"), systemImage: "terminal")
                }.padding(.horizontal, 4)
            }.padding(24)
        }.frame(minWidth: 660, idealWidth: 740, minHeight: 580, idealHeight: 680)
            .background(Color(nsColor: .windowBackgroundColor))
            .environment(\.locale, L10n.locale)
            .onAppear { model.refreshTerminal() }
    }
}

@MainActor final class AppDelegate: NSObject, NSApplicationDelegate {
    var model: ScanModel?
    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { true }
    func applicationShouldTerminate(_ sender: NSApplication) -> NSApplication.TerminateReply {
        guard let model, model.running else { return .terminateNow }
        let alert = NSAlert()
        alert.messageText = L10n.text("Cancel alignment and quit ScanMerge?")
        alert.informativeText = L10n.text(
            "Source scans will remain unchanged. An incomplete result will not be saved.")
        alert.addButton(withTitle: L10n.text("Continue processing"))
        alert.addButton(withTitle: L10n.text("Cancel and quit"))
        guard alert.runModal() == .alertSecondButtonReturn else { return .terminateCancel }
        model.afterFinish = { sender.reply(toApplicationShouldTerminate: true) }
        model.cancel()
        return .terminateLater
    }
}

#if !GUI_TESTS
    @main struct ScanMergeApplication: App {
        @NSApplicationDelegateAdaptor(AppDelegate.self) var delegate
        @StateObject private var model = ScanModel()
        var body: some Scene {
            Window("ScanMerge", id: "main") {
                MainView(model: model).onAppear { delegate.model = model }
            }.defaultSize(width: 740, height: 680)
                .commands { CommandGroup(replacing: .newItem) {} }
        }
    }
#endif
