void head(char *title) {
    cool_html_raw(COOL_SV("    <head>\n        <title>"));
    cool_html_txt(title, strlen(title));
    cool_html_raw(COOL_SV("</title>\n        <link rel=\"stylesheet\" href=\"style.css\">\n    </head>\n"));
}


void footer(void) {
    cool_html_raw(COOL_SV("    <footer>\n        copyright @ Vasco Alves 2026\n    </footer>\n"));
}


void page(char *title, void(*func)(char *), char *p) {
    cool_html_raw(COOL_SV("    <!DOCTYPE html>\n    <html>\n        "));
    head(title);
    cool_html_raw(COOL_SV("\n        <body>\n            <main>\n                "));
    func(p);
    cool_html_raw(COOL_SV("\n            </main>\n            "));
    footer();
    cool_html_raw(COOL_SV("\n        </body>\n    </html>\n"));
}

