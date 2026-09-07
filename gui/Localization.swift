// SPDX-License-Identifier: GPL-3.0-only

import Foundation

enum L10n {
    static func language(for preferredLanguages: [String]) -> String {
        let primary = preferredLanguages.first ?? "en"
        let base = primary.replacingOccurrences(of: "_", with: "-").split(separator: "-").first
        return base?.lowercased() == "pl" ? "pl" : "en"
    }

    static let language = language(for: Locale.preferredLanguages)
    static var resources = Bundle.main
    static var locale: Locale { Locale(identifier: language == "pl" ? "pl_PL" : "en_US") }

    static func text(_ key: String, _ arguments: CVarArg...) -> String {
        let translated: String
        if let path = resources.path(forResource: language, ofType: "lproj"),
            let bundle = Bundle(path: path)
        {
            translated = bundle.localizedString(forKey: key, value: key, table: nil)
        } else {
            translated = key
        }
        return arguments.isEmpty
            ? translated : String(format: translated, locale: locale, arguments: arguments)
    }
}
