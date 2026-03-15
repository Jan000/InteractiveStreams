#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <random>
#include <cctype>

namespace is::games::country_elimination {

// ── Alias table: maps many names/codes to ISO 2-letter code ──────────────────
// Every key must be UPPERCASE. The value is the 2-letter ISO code used in SPRITE_ORDER.

inline const std::unordered_map<std::string, std::string>& getCountryAliases() {
    static const std::unordered_map<std::string, std::string> aliases = {
        // ── Africa ──────────────────────────────────────────────────────────
        // Algeria
        {"DZ", "DZ"}, {"DZA", "DZ"}, {"ALGERIA", "DZ"}, {"ALGERIEN", "DZ"}, {"ALGERIE", "DZ"},
        // Angola
        {"AO", "AO"}, {"AGO", "AO"}, {"ANGOLA", "AO"},
        // Benin
        {"BJ", "BJ"}, {"BEN", "BJ"}, {"BENIN", "BJ"},
        // Botswana
        {"BW", "BW"}, {"BWA", "BW"}, {"BOTSWANA", "BW"}, {"BOTSUANA", "BW"},
        // Burkina Faso
        {"BF", "BF"}, {"BFA", "BF"}, {"BURKINA FASO", "BF"}, {"BURKINA", "BF"},
        // Burundi
        {"BI", "BI"}, {"BDI", "BI"}, {"BURUNDI", "BI"},
        // Cameroon
        {"CM", "CM"}, {"CMR", "CM"}, {"CAMEROON", "CM"}, {"KAMERUN", "CM"}, {"CAMEROUN", "CM"},
        // Cape Verde
        {"CV", "CV"}, {"CPV", "CV"}, {"CABO VERDE", "CV"}, {"CAPE VERDE", "CV"}, {"KAP VERDE", "CV"},
        // Central African Republic
        {"CF", "CF"}, {"CAF", "CF"}, {"CENTRAL AFRICAN REPUBLIC", "CF"}, {"ZENTRALAFRIKANISCHE REPUBLIK", "CF"},
        // Chad
        {"TD", "TD"}, {"TCD", "TD"}, {"CHAD", "TD"}, {"TSCHAD", "TD"}, {"TCHAD", "TD"},
        // DR Congo
        {"CD", "CD"}, {"COD", "CD"}, {"DR CONGO", "CD"}, {"CONGO DR", "CD"}, {"DRC", "CD"}, {"KONGO", "CD"},
        // Djibouti
        {"DJ", "DJ"}, {"DJI", "DJ"}, {"DJIBOUTI", "DJ"}, {"DSCHIBUTI", "DJ"},
        // Egypt
        {"EG", "EG"}, {"EGY", "EG"}, {"EGYPT", "EG"}, {"AGYPTEN", "EG"}, {"AEGYPTEN", "EG"}, {"EGYPTE", "EG"}, {"EGIPTO", "EG"},
        // Equatorial Guinea
        {"GQ", "GQ"}, {"GNQ", "GQ"}, {"EQUATORIAL GUINEA", "GQ"}, {"AQUATORIALGUINEA", "GQ"},
        // Eritrea
        {"ER", "ER"}, {"ERI", "ER"}, {"ERITREA", "ER"},
        // Ethiopia
        {"ET", "ET"}, {"ETH", "ET"}, {"ETHIOPIA", "ET"}, {"ATHIOPIEN", "ET"}, {"ETHIOPIE", "ET"},
        // Gabon
        {"GA", "GA"}, {"GAB", "GA"}, {"GABON", "GA"}, {"GABUN", "GA"},
        // Gambia
        {"GM", "GM"}, {"GMB", "GM"}, {"GAMBIA", "GM"},
        // Ghana
        {"GH", "GH"}, {"GHA", "GH"}, {"GHANA", "GH"},
        // Guinea
        {"GN", "GN"}, {"GIN", "GN"}, {"GUINEA", "GN"},
        // Guinea-Bissau
        {"GW", "GW"}, {"GNB", "GW"}, {"GUINEA-BISSAU", "GW"}, {"GUINEA BISSAU", "GW"},
        // Ivory Coast
        {"CI", "CI"}, {"CIV", "CI"}, {"IVORY COAST", "CI"}, {"COTE D'IVOIRE", "CI"}, {"ELFENBEINKUSTE", "CI"},
        // Kenya
        {"KE", "KE"}, {"KEN", "KE"}, {"KENYA", "KE"}, {"KENIA", "KE"},
        // Lesotho
        {"LS", "LS"}, {"LSO", "LS"}, {"LESOTHO", "LS"},
        // Liberia
        {"LR", "LR"}, {"LBR", "LR"}, {"LIBERIA", "LR"},
        // Libya
        {"LY", "LY"}, {"LBY", "LY"}, {"LIBYA", "LY"}, {"LIBYEN", "LY"}, {"LIBYE", "LY"},
        // Madagascar
        {"MG", "MG"}, {"MDG", "MG"}, {"MADAGASCAR", "MG"}, {"MADAGASKAR", "MG"},
        // Malawi
        {"MW", "MW"}, {"MWI", "MW"}, {"MALAWI", "MW"},
        // Mali
        {"ML", "ML"}, {"MLI", "ML"}, {"MALI", "ML"},
        // Mauritania
        {"MR", "MR"}, {"MRT", "MR"}, {"MAURITANIA", "MR"}, {"MAURETANIEN", "MR"},
        // Mauritius
        {"MU", "MU"}, {"MUS", "MU"}, {"MAURITIUS", "MU"},
        // Mayotte
        {"YT", "YT"}, {"MYT", "YT"}, {"MAYOTTE", "YT"},
        // Morocco
        {"MA", "MA"}, {"MAR", "MA"}, {"MOROCCO", "MA"}, {"MAROKKO", "MA"}, {"MAROC", "MA"}, {"MARRUECOS", "MA"},
        // Mozambique
        {"MZ", "MZ"}, {"MOZ", "MZ"}, {"MOZAMBIQUE", "MZ"}, {"MOSAMBIK", "MZ"},
        // Namibia
        {"NA", "NA"}, {"NAM", "NA"}, {"NAMIBIA", "NA"},
        // Niger
        {"NE", "NE"}, {"NER", "NE"}, {"NIGER", "NE"},
        // Nigeria
        {"NG", "NG"}, {"NGA", "NG"}, {"NIGERIA", "NG"},
        // Congo Republic
        {"CG", "CG"}, {"COG", "CG"}, {"CONGO", "CG"}, {"REPUBLIC OF CONGO", "CG"}, {"REPUBLIK KONGO", "CG"},
        // Reunion
        {"RE", "RE"}, {"REU", "RE"}, {"REUNION", "RE"},
        // Rwanda
        {"RW", "RW"}, {"RWA", "RW"}, {"RWANDA", "RW"}, {"RUANDA", "RW"},
        // Saint Helena
        {"SH", "SH"}, {"SHN", "SH"}, {"SAINT HELENA", "SH"}, {"ST HELENA", "SH"},
        // Sao Tome
        {"ST", "ST"}, {"STP", "ST"}, {"SAO TOME", "ST"}, {"SAO TOME AND PRINCIPE", "ST"},
        // Senegal
        {"SN", "SN"}, {"SEN", "SN"}, {"SENEGAL", "SN"},
        // Seychelles
        {"SC", "SC"}, {"SYC", "SC"}, {"SEYCHELLES", "SC"}, {"SEYCHELLEN", "SC"},
        // Sierra Leone
        {"SL", "SL"}, {"SLE", "SL"}, {"SIERRA LEONE", "SL"},
        // Somalia
        {"SO", "SO"}, {"SOM", "SO"}, {"SOMALIA", "SO"},
        // South Africa
        {"ZA", "ZA"}, {"ZAF", "ZA"}, {"SOUTH AFRICA", "ZA"}, {"SUDAFRIKA", "ZA"}, {"SUEDAFRIKA", "ZA"}, {"AFRIQUE DU SUD", "ZA"},
        // South Sudan
        {"SS", "SS"}, {"SSD", "SS"}, {"SOUTH SUDAN", "SS"}, {"SUDSUDAN", "SS"}, {"SUEDSUDAN", "SS"},
        // Sudan
        {"SD", "SD"}, {"SDN", "SD"}, {"SUDAN", "SD"},
        // Suriname
        {"SR", "SR"}, {"SUR", "SR"}, {"SURINAME", "SR"}, {"SURINAM", "SR"},
        // Eswatini
        {"SZ", "SZ"}, {"SWZ", "SZ"}, {"ESWATINI", "SZ"}, {"SWAZILAND", "SZ"}, {"SWASILAND", "SZ"},
        // Togo
        {"TG", "TG"}, {"TGO", "TG"}, {"TOGO", "TG"},
        // Tunisia
        {"TN", "TN"}, {"TUN", "TN"}, {"TUNISIA", "TN"}, {"TUNESIEN", "TN"}, {"TUNISIE", "TN"},
        // Uganda
        {"UG", "UG"}, {"UGA", "UG"}, {"UGANDA", "UG"},
        // Tanzania
        {"TZ", "TZ"}, {"TZA", "TZ"}, {"TANZANIA", "TZ"}, {"TANSANIA", "TZ"}, {"TANZANIE", "TZ"},
        // Western Sahara
        {"EH", "EH"}, {"ESH", "EH"}, {"WESTERN SAHARA", "EH"}, {"WESTSAHARA", "EH"},
        // Yemen
        {"YE", "YE"}, {"YEM", "YE"}, {"YEMEN", "YE"}, {"JEMEN", "YE"},
        // Zambia
        {"ZM", "ZM"}, {"ZMB", "ZM"}, {"ZAMBIA", "ZM"}, {"SAMBIA", "ZM"},
        // Zimbabwe
        {"ZW", "ZW"}, {"ZWE", "ZW"}, {"ZIMBABWE", "ZW"}, {"SIMBABWE", "ZW"},

        // ── Americas ────────────────────────────────────────────────────────
        // Anguilla
        {"AI", "AI"}, {"AIA", "AI"}, {"ANGUILLA", "AI"},
        // Antigua
        {"AG", "AG"}, {"ATG", "AG"}, {"ANTIGUA", "AG"}, {"ANTIGUA AND BARBUDA", "AG"},
        // Argentina
        {"AR", "AR"}, {"ARG", "AR"}, {"ARGENTINA", "AR"}, {"ARGENTINIEN", "AR"}, {"ARGENTINE", "AR"},
        // Aruba
        {"AW", "AW"}, {"ABW", "AW"}, {"ARUBA", "AW"},
        // Bahamas
        {"BS", "BS"}, {"BHS", "BS"}, {"BAHAMAS", "BS"},
        // Barbados
        {"BB", "BB"}, {"BRB", "BB"}, {"BARBADOS", "BB"},
        // Bonaire
        {"BQ", "BQ"}, {"BES", "BQ"}, {"BONAIRE", "BQ"},
        // Belize
        {"BZ", "BZ"}, {"BLZ", "BZ"}, {"BELIZE", "BZ"},
        // Bermuda
        {"BM", "BM"}, {"BMU", "BM"}, {"BERMUDA", "BM"},
        // Bolivia
        {"BO", "BO"}, {"BOL", "BO"}, {"BOLIVIA", "BO"}, {"BOLIVIEN", "BO"}, {"BOLIVIE", "BO"},
        // British Virgin Islands
        {"VG", "VG"}, {"VGB", "VG"}, {"BRITISH VIRGIN ISLANDS", "VG"},
        // Brazil
        {"BR", "BR"}, {"BRA", "BR"}, {"BRAZIL", "BR"}, {"BRASILIEN", "BR"}, {"BRESIL", "BR"}, {"BRASIL", "BR"},
        // Canada
        {"CA", "CA"}, {"CAN", "CA"}, {"CANADA", "CA"}, {"KANADA", "CA"},
        // Cayman Islands
        {"KY", "KY"}, {"CYM", "KY"}, {"CAYMAN ISLANDS", "KY"}, {"CAYMAN", "KY"}, {"KAIMANINSELN", "KY"},
        // Chile
        {"CL", "CL"}, {"CHL", "CL"}, {"CHILE", "CL"},
        // Colombia
        {"CO", "CO"}, {"COL", "CO"}, {"COLOMBIA", "CO"}, {"KOLUMBIEN", "CO"}, {"COLOMBIE", "CO"},
        // Comoros
        {"KM", "KM"}, {"COM", "KM"}, {"COMOROS", "KM"}, {"KOMOREN", "KM"},
        // Costa Rica
        {"CR", "CR"}, {"CRI", "CR"}, {"COSTA RICA", "CR"},
        // Cuba
        {"CU", "CU"}, {"CUB", "CU"}, {"CUBA", "CU"}, {"KUBA", "CU"},
        // Curacao
        {"CW", "CW"}, {"CUW", "CW"}, {"CURACAO", "CW"},
        // Dominica
        {"DM", "DM"}, {"DMA", "DM"}, {"DOMINICA", "DM"},
        // Dominican Republic
        {"DO", "DO"}, {"DOM", "DO"}, {"DOMINICAN REPUBLIC", "DO"}, {"DOMINIKANISCHE REPUBLIK", "DO"},
        // Ecuador
        {"EC", "EC"}, {"ECU", "EC"}, {"ECUADOR", "EC"},
        // El Salvador
        {"SV", "SV"}, {"SLV", "SV"}, {"EL SALVADOR", "SV"}, {"SALVADOR", "SV"},
        // Falkland Islands
        {"FK", "FK"}, {"FLK", "FK"}, {"FALKLAND ISLANDS", "FK"}, {"FALKLANDINSELN", "FK"}, {"MALVINAS", "FK"},
        // French Guiana
        {"GF", "GF"}, {"GUF", "GF"}, {"FRENCH GUIANA", "GF"}, {"FRANZOSISCH-GUAYANA", "GF"},
        // Greenland
        {"GL", "GL"}, {"GRL", "GL"}, {"GREENLAND", "GL"}, {"GRONLAND", "GL"}, {"GROENLAND", "GL"},
        // Grenada
        {"GD", "GD"}, {"GRD", "GD"}, {"GRENADA", "GD"},
        // Guadeloupe
        {"GP", "GP"}, {"GLP", "GP"}, {"GUADELOUPE", "GP"},
        // Guatemala
        {"GT", "GT"}, {"GTM", "GT"}, {"GUATEMALA", "GT"},
        // Guyana
        {"GY", "GY"}, {"GUY", "GY"}, {"GUYANA", "GY"},
        // Haiti
        {"HT", "HT"}, {"HTI", "HT"}, {"HAITI", "HT"},
        // Honduras
        {"HN", "HN"}, {"HND", "HN"}, {"HONDURAS", "HN"},
        // Jamaica
        {"JM", "JM"}, {"JAM", "JM"}, {"JAMAICA", "JM"}, {"JAMAIKA", "JM"},
        // Martinique
        {"MQ", "MQ"}, {"MTQ", "MQ"}, {"MARTINIQUE", "MQ"},
        // Mexico
        {"MX", "MX"}, {"MEX", "MX"}, {"MEXICO", "MX"}, {"MEXIKO", "MX"}, {"MEXIQUE", "MX"},
        // Montserrat
        {"MS", "MS"}, {"MSR", "MS"}, {"MONTSERRAT", "MS"},
        // Nicaragua
        {"NI", "NI"}, {"NIC", "NI"}, {"NICARAGUA", "NI"},
        // Panama
        {"PA", "PA"}, {"PAN", "PA"}, {"PANAMA", "PA"},
        // Paraguay
        {"PY", "PY"}, {"PRY", "PY"}, {"PARAGUAY", "PY"},
        // Peru
        {"PE", "PE"}, {"PER", "PE"}, {"PERU", "PE"},
        // Puerto Rico
        {"PR", "PR"}, {"PRI", "PR"}, {"PUERTO RICO", "PR"},
        // Saint Barthelemy
        {"BL", "BL"}, {"BLM", "BL"}, {"SAINT BARTHELEMY", "BL"},
        // Saint Kitts
        {"KN", "KN"}, {"KNA", "KN"}, {"SAINT KITTS", "KN"}, {"SAINT KITTS AND NEVIS", "KN"}, {"ST KITTS", "KN"},
        // Saint Lucia
        {"LC", "LC"}, {"LCA", "LC"}, {"SAINT LUCIA", "LC"}, {"ST LUCIA", "LC"},
        // Saint Pierre
        {"PM", "PM"}, {"SPM", "PM"}, {"SAINT PIERRE", "PM"}, {"SAINT PIERRE AND MIQUELON", "PM"},
        // Saint Vincent
        {"VC", "VC"}, {"VCT", "VC"}, {"SAINT VINCENT", "VC"}, {"ST VINCENT", "VC"},
        // Sint Maarten
        {"SX", "SX"}, {"SXM", "SX"}, {"SINT MAARTEN", "SX"},
        // Trinidad and Tobago
        {"TT", "TT"}, {"TTO", "TT"}, {"TRINIDAD", "TT"}, {"TRINIDAD AND TOBAGO", "TT"},
        // Turks and Caicos
        {"TC", "TC"}, {"TCA", "TC"}, {"TURKS AND CAICOS", "TC"},
        // United States
        {"US", "US"}, {"USA", "US"}, {"UNITED STATES", "US"}, {"AMERICA", "US"}, {"AMERIKA", "US"},
        {"VEREINIGTE STAATEN", "US"}, {"ESTADOS UNIDOS", "US"}, {"ETATS-UNIS", "US"}, {"ETATS UNIS", "US"},
        // US Virgin Islands
        {"VI", "VI"}, {"VIR", "VI"}, {"US VIRGIN ISLANDS", "VI"},
        // Uruguay
        {"UY", "UY"}, {"URY", "UY"}, {"URUGUAY", "UY"},
        // Venezuela
        {"VE", "VE"}, {"VEN", "VE"}, {"VENEZUELA", "VE"},

        // ── Asia ────────────────────────────────────────────────────────────
        // Abkhazia
        {"AB", "AB"}, {"ABKHAZIA", "AB"}, {"ABCHASIEN", "AB"},
        // Afghanistan
        {"AF", "AF"}, {"AFG", "AF"}, {"AFGHANISTAN", "AF"},
        // Azerbaijan
        {"AZ", "AZ"}, {"AZE", "AZ"}, {"AZERBAIJAN", "AZ"}, {"ASERBAIDSCHAN", "AZ"}, {"AZERBAIDJAN", "AZ"},
        // Bangladesh
        {"BD", "BD"}, {"BGD", "BD"}, {"BANGLADESH", "BD"}, {"BANGLADESCH", "BD"},
        // Bhutan
        {"BT", "BT"}, {"BTN", "BT"}, {"BHUTAN", "BT"},
        // Brunei
        {"BN", "BN"}, {"BRN", "BN"}, {"BRUNEI", "BN"},
        // Cambodia
        {"KH", "KH"}, {"KHM", "KH"}, {"CAMBODIA", "KH"}, {"KAMBODSCHA", "KH"}, {"CAMBODGE", "KH"},
        // China
        {"CN", "CN"}, {"CHN", "CN"}, {"CHINA", "CN"},
        // Georgia
        {"GE", "GE"}, {"GEO", "GE"}, {"GEORGIA", "GE"}, {"GEORGIEN", "GE"}, {"GEORGIE", "GE"},
        // Hong Kong
        {"HK", "HK"}, {"HKG", "HK"}, {"HONG KONG", "HK"}, {"HONGKONG", "HK"},
        // India
        {"IN", "IN"}, {"IND", "IN"}, {"INDIA", "IN"}, {"INDIEN", "IN"}, {"INDE", "IN"},
        // Indonesia
        {"ID", "ID"}, {"IDN", "ID"}, {"INDONESIA", "ID"}, {"INDONESIEN", "ID"}, {"INDONESIE", "ID"},
        // Japan
        {"JP", "JP"}, {"JPN", "JP"}, {"JAPAN", "JP"}, {"JAPON", "JP"},
        // Kazakhstan
        {"KZ", "KZ"}, {"KAZ", "KZ"}, {"KAZAKHSTAN", "KZ"}, {"KASACHSTAN", "KZ"},
        // Laos
        {"LA", "LA"}, {"LAO", "LA"}, {"LAOS", "LA"},
        // Macau
        {"MO", "MO"}, {"MAC", "MO"}, {"MACAU", "MO"}, {"MACAO", "MO"},
        // Malaysia
        {"MY", "MY"}, {"MYS", "MY"}, {"MALAYSIA", "MY"},
        // Maldives
        {"MV", "MV"}, {"MDV", "MV"}, {"MALDIVES", "MV"}, {"MALEDIVEN", "MV"},
        // Mongolia
        {"MN", "MN"}, {"MNG", "MN"}, {"MONGOLIA", "MN"}, {"MONGOLEI", "MN"}, {"MONGOLIE", "MN"},
        // Myanmar
        {"MM", "MM"}, {"MMR", "MM"}, {"MYANMAR", "MM"}, {"BURMA", "MM"},
        // Nepal
        {"NP", "NP"}, {"NPL", "NP"}, {"NEPAL", "NP"},
        // North Korea
        {"KP", "KP"}, {"PRK", "KP"}, {"NORTH KOREA", "KP"}, {"NORDKOREA", "KP"}, {"COREE DU NORD", "KP"},
        // Northern Mariana Islands
        {"MP", "MP"}, {"MNP", "MP"}, {"NORTHERN MARIANA ISLANDS", "MP"},
        // Palau
        {"PW", "PW"}, {"PLW", "PW"}, {"PALAU", "PW"},
        // Papua New Guinea
        {"PG", "PG"}, {"PNG", "PG"}, {"PAPUA NEW GUINEA", "PG"}, {"PAPUA-NEUGUINEA", "PG"},
        // Philippines
        {"PH", "PH"}, {"PHL", "PH"}, {"PHILIPPINES", "PH"}, {"PHILIPPINEN", "PH"},
        // Singapore
        {"SG", "SG"}, {"SGP", "SG"}, {"SINGAPORE", "SG"}, {"SINGAPUR", "SG"}, {"SINGAPOUR", "SG"},
        // South Korea
        {"KR", "KR"}, {"KOR", "KR"}, {"SOUTH KOREA", "KR"}, {"KOREA", "KR"}, {"SUDKOREA", "KR"}, {"SUEDKOREA", "KR"}, {"COREE DU SUD", "KR"},
        // Sri Lanka
        {"LK", "LK"}, {"LKA", "LK"}, {"SRI LANKA", "LK"},
        // Taiwan
        {"TW", "TW"}, {"TWN", "TW"}, {"TAIWAN", "TW"},
        // Tajikistan
        {"TJ", "TJ"}, {"TJK", "TJ"}, {"TAJIKISTAN", "TJ"}, {"TADSCHIKISTAN", "TJ"},
        // Thailand
        {"TH", "TH"}, {"THA", "TH"}, {"THAILAND", "TH"},
        // Timor-Leste
        {"TL", "TL"}, {"TLS", "TL"}, {"TIMOR-LESTE", "TL"}, {"EAST TIMOR", "TL"}, {"OSTTIMOR", "TL"},
        // Turkmenistan
        {"TM", "TM"}, {"TKM", "TM"}, {"TURKMENISTAN", "TM"},
        // Vietnam
        {"VN", "VN"}, {"VNM", "VN"}, {"VIETNAM", "VN"}, {"VIET NAM", "VN"},

        // ── Europe ──────────────────────────────────────────────────────────
        // Aland Islands
        {"AX", "AX"}, {"ALA", "AX"}, {"ALAND", "AX"}, {"ALAND ISLANDS", "AX"},
        // Albania
        {"AL", "AL"}, {"ALB", "AL"}, {"ALBANIA", "AL"}, {"ALBANIEN", "AL"}, {"ALBANIE", "AL"},
        // Andorra
        {"AD", "AD"}, {"AND", "AD"}, {"ANDORRA", "AD"},
        // Armenia
        {"AM", "AM"}, {"ARM", "AM"}, {"ARMENIA", "AM"}, {"ARMENIEN", "AM"}, {"ARMENIE", "AM"},
        // Austria
        {"AT", "AT"}, {"AUT", "AT"}, {"AUSTRIA", "AT"}, {"OSTERREICH", "AT"}, {"OESTERREICH", "AT"}, {"AUTRICHE", "AT"},
        // Belarus
        {"BY", "BY"}, {"BLR", "BY"}, {"BELARUS", "BY"}, {"WEISSRUSSLAND", "BY"}, {"BIELORUSSIE", "BY"},
        // Belgium
        {"BE", "BE"}, {"BEL", "BE"}, {"BELGIUM", "BE"}, {"BELGIEN", "BE"}, {"BELGIQUE", "BE"},
        // Bosnia
        {"BA", "BA"}, {"BIH", "BA"}, {"BOSNIA", "BA"}, {"BOSNIA AND HERZEGOVINA", "BA"}, {"BOSNIEN", "BA"}, {"BOSNIE", "BA"},
        // Bulgaria
        {"BG", "BG"}, {"BGR", "BG"}, {"BULGARIA", "BG"}, {"BULGARIEN", "BG"}, {"BULGARIE", "BG"},
        // Croatia
        {"HR", "HR"}, {"HRV", "HR"}, {"CROATIA", "HR"}, {"KROATIEN", "HR"}, {"CROATIE", "HR"}, {"HRVATSKA", "HR"},
        // Cyprus
        {"CY", "CY"}, {"CYP", "CY"}, {"CYPRUS", "CY"}, {"ZYPERN", "CY"}, {"CHYPRE", "CY"},
        // Czech Republic
        {"CZ", "CZ"}, {"CZE", "CZ"}, {"CZECH REPUBLIC", "CZ"}, {"CZECHIA", "CZ"}, {"TSCHECHIEN", "CZ"}, {"REPUBLIQUE TCHEQUE", "CZ"},
        // Denmark
        {"DK", "DK"}, {"DNK", "DK"}, {"DENMARK", "DK"}, {"DANEMARK", "DK"}, {"DAENEMARK", "DK"}, {"DANEMARK", "DK"},
        // Estonia
        {"EE", "EE"}, {"EST", "EE"}, {"ESTONIA", "EE"}, {"ESTLAND", "EE"}, {"ESTONIE", "EE"},
        // Faroe Islands
        {"FO", "FO"}, {"FRO", "FO"}, {"FAROE ISLANDS", "FO"}, {"FAROER", "FO"}, {"FAEROER", "FO"},
        // Finland
        {"FI", "FI"}, {"FIN", "FI"}, {"FINLAND", "FI"}, {"FINNLAND", "FI"}, {"FINLANDE", "FI"}, {"SUOMI", "FI"},
        // France
        {"FR", "FR"}, {"FRA", "FR"}, {"FRANCE", "FR"}, {"FRANKREICH", "FR"}, {"FRANCIA", "FR"},
        // Germany
        {"DE", "DE"}, {"DEU", "DE"}, {"GER", "DE"}, {"GERMANY", "DE"}, {"DEUTSCHLAND", "DE"}, {"ALLEMAGNE", "DE"}, {"ALEMANIA", "DE"},
        // Gibraltar
        {"GI", "GI"}, {"GIB", "GI"}, {"GIBRALTAR", "GI"},
        // Greece
        {"GR", "GR"}, {"GRC", "GR"}, {"GREECE", "GR"}, {"GRIECHENLAND", "GR"}, {"GRECE", "GR"}, {"HELLAS", "GR"},
        // Guernsey
        {"GG", "GG"}, {"GGY", "GG"}, {"GUERNSEY", "GG"},
        // Hungary
        {"HU", "HU"}, {"HUN", "HU"}, {"HUNGARY", "HU"}, {"UNGARN", "HU"}, {"HONGRIE", "HU"},
        // Iceland
        {"IS", "IS"}, {"ISL", "IS"}, {"ICELAND", "IS"}, {"ISLAND", "IS"}, {"ISLANDE", "IS"},
        // Ireland
        {"IE", "IE"}, {"IRL", "IE"}, {"IRELAND", "IE"}, {"IRLAND", "IE"}, {"IRLANDE", "IE"},
        // Isle of Man
        {"IM", "IM"}, {"IMN", "IM"}, {"ISLE OF MAN", "IM"},
        // Italy
        {"IT", "IT"}, {"ITA", "IT"}, {"ITALY", "IT"}, {"ITALIEN", "IT"}, {"ITALIE", "IT"}, {"ITALIA", "IT"},
        // Jersey
        {"JE", "JE"}, {"JEY", "JE"}, {"JERSEY", "JE"},
        // Kosovo
        {"XK", "XK"}, {"XKX", "XK"}, {"KOSOVO", "XK"},
        // Latvia
        {"LV", "LV"}, {"LVA", "LV"}, {"LATVIA", "LV"}, {"LETTLAND", "LV"}, {"LETTONIE", "LV"},
        // Liechtenstein
        {"LI", "LI"}, {"LIE", "LI"}, {"LIECHTENSTEIN", "LI"},
        // Lithuania
        {"LT", "LT"}, {"LTU", "LT"}, {"LITHUANIA", "LT"}, {"LITAUEN", "LT"}, {"LITUANIE", "LT"},
        // Luxembourg
        {"LU", "LU"}, {"LUX", "LU"}, {"LUXEMBOURG", "LU"}, {"LUXEMBURG", "LU"},
        // Malta
        {"MT", "MT"}, {"MLT", "MT"}, {"MALTA", "MT"},
        // Moldova
        {"MD", "MD"}, {"MDA", "MD"}, {"MOLDOVA", "MD"}, {"MOLDAWIEN", "MD"}, {"MOLDAVIE", "MD"},
        // Monaco
        {"MC", "MC"}, {"MCO", "MC"}, {"MONACO", "MC"},
        // Montenegro
        {"ME", "ME"}, {"MNE", "ME"}, {"MONTENEGRO", "ME"},
        // Netherlands
        {"NL", "NL"}, {"NLD", "NL"}, {"NETHERLANDS", "NL"}, {"HOLLAND", "NL"}, {"NIEDERLANDE", "NL"}, {"PAYS-BAS", "NL"}, {"PAISES BAJOS", "NL"},
        // North Macedonia
        {"MK", "MK"}, {"MKD", "MK"}, {"NORTH MACEDONIA", "MK"}, {"MACEDONIA", "MK"}, {"NORDMAZEDONIEN", "MK"}, {"MACEDOINE", "MK"},
        // Norway
        {"NO", "NO"}, {"NOR", "NO"}, {"NORWAY", "NO"}, {"NORWEGEN", "NO"}, {"NORVEGE", "NO"},
        // Poland
        {"PL", "PL"}, {"POL", "PL"}, {"POLAND", "PL"}, {"POLEN", "PL"}, {"POLOGNE", "PL"}, {"POLSKA", "PL"},
        // Portugal
        {"PT", "PT"}, {"PRT", "PT"}, {"PORTUGAL", "PT"},
        // Romania
        {"RO", "RO"}, {"ROU", "RO"}, {"ROMANIA", "RO"}, {"RUMANIEN", "RO"}, {"ROUMANIE", "RO"},
        // Russia
        {"RU", "RU"}, {"RUS", "RU"}, {"RUSSIA", "RU"}, {"RUSSLAND", "RU"}, {"RUSSIE", "RU"}, {"RUSIA", "RU"},
        // San Marino
        {"SM", "SM"}, {"SMR", "SM"}, {"SAN MARINO", "SM"},
        // Serbia
        {"RS", "RS"}, {"SRB", "RS"}, {"SERBIA", "RS"}, {"SERBIEN", "RS"}, {"SERBIE", "RS"},
        // Slovakia
        {"SK", "SK"}, {"SVK", "SK"}, {"SLOVAKIA", "SK"}, {"SLOWAKEI", "SK"}, {"SLOVAQUIE", "SK"},
        // Slovenia
        {"SI", "SI"}, {"SVN", "SI"}, {"SLOVENIA", "SI"}, {"SLOWENIEN", "SI"}, {"SLOVENIE", "SI"},
        // Spain
        {"ES", "ES"}, {"ESP", "ES"}, {"SPAIN", "ES"}, {"SPANIEN", "ES"}, {"ESPAGNE", "ES"}, {"ESPANA", "ES"},
        // Sweden
        {"SE", "SE"}, {"SWE", "SE"}, {"SWEDEN", "SE"}, {"SCHWEDEN", "SE"}, {"SUEDE", "SE"}, {"SUECIA", "SE"},
        // Switzerland
        {"CH", "CH"}, {"CHE", "CH"}, {"SWITZERLAND", "CH"}, {"SCHWEIZ", "CH"}, {"SUISSE", "CH"}, {"SVIZZERA", "CH"}, {"SUIZA", "CH"},
        // Turkey
        {"TR", "TR"}, {"TUR", "TR"}, {"TURKEY", "TR"}, {"TURKEI", "TR"}, {"TUERKEI", "TR"}, {"TURQUIE", "TR"}, {"TURQUIA", "TR"},
        // Ukraine
        {"UA", "UA"}, {"UKR", "UA"}, {"UKRAINE", "UA"},
        // United Kingdom
        {"GB", "GB"}, {"GBR", "GB"}, {"UK", "GB"}, {"UNITED KINGDOM", "GB"}, {"ENGLAND", "GB"}, {"GROSSBRITANNIEN", "GB"},
        {"GREAT BRITAIN", "GB"}, {"ROYAUME-UNI", "GB"}, {"REINO UNIDO", "GB"},
        // Vatican
        {"VA", "VA"}, {"VAT", "VA"}, {"VATICAN", "VA"}, {"VATIKAN", "VA"},

        // ── Middle East ─────────────────────────────────────────────────────
        // Bahrain
        {"BH", "BH"}, {"BHR", "BH"}, {"BAHRAIN", "BH"},
        // Iran
        {"IR", "IR"}, {"IRN", "IR"}, {"IRAN", "IR"},
        // Iraq
        {"IQ", "IQ"}, {"IRQ", "IQ"}, {"IRAQ", "IQ"}, {"IRAK", "IQ"},
        // Israel
        {"IL", "IL"}, {"ISR", "IL"}, {"ISRAEL", "IL"},
        // Kuwait
        {"KW", "KW"}, {"KWT", "KW"}, {"KUWAIT", "KW"},
        // Jordan
        {"JO", "JO"}, {"JOR", "JO"}, {"JORDAN", "JO"}, {"JORDANIEN", "JO"}, {"JORDANIE", "JO"},
        // Kyrgyzstan
        {"KG", "KG"}, {"KGZ", "KG"}, {"KYRGYZSTAN", "KG"}, {"KIRGISISTAN", "KG"},
        // Lebanon
        {"LB", "LB"}, {"LBN", "LB"}, {"LEBANON", "LB"}, {"LIBANON", "LB"}, {"LIBAN", "LB"},
        // Oman
        {"OM", "OM"}, {"OMN", "OM"}, {"OMAN", "OM"},
        // Pakistan
        {"PK", "PK"}, {"PAK", "PK"}, {"PAKISTAN", "PK"},
        // Palestine
        {"PS", "PS"}, {"PSE", "PS"}, {"PALESTINE", "PS"}, {"PALASTINA", "PS"}, {"PALAESTINA", "PS"},
        // Qatar
        {"QA", "QA"}, {"QAT", "QA"}, {"QATAR", "QA"}, {"KATAR", "QA"},
        // Saudi Arabia
        {"SA", "SA"}, {"SAU", "SA"}, {"SAUDI ARABIA", "SA"}, {"SAUDI-ARABIEN", "SA"}, {"ARABIE SAOUDITE", "SA"},
        // Syria
        {"SY", "SY"}, {"SYR", "SY"}, {"SYRIA", "SY"}, {"SYRIEN", "SY"}, {"SYRIE", "SY"},
        // UAE
        {"AE", "AE"}, {"ARE", "AE"}, {"UAE", "AE"}, {"UNITED ARAB EMIRATES", "AE"}, {"VEREINIGTE ARABISCHE EMIRATE", "AE"}, {"EMIRATS ARABES UNIS", "AE"},
        // Uzbekistan
        {"UZ", "UZ"}, {"UZB", "UZ"}, {"UZBEKISTAN", "UZ"}, {"USBEKISTAN", "UZ"},

        // ── Oceania ─────────────────────────────────────────────────────────
        // American Samoa
        {"AS", "AS"}, {"ASM", "AS"}, {"AMERICAN SAMOA", "AS"},
        // Australia
        {"AU", "AU"}, {"AUS", "AU"}, {"AUSTRALIA", "AU"}, {"AUSTRALIEN", "AU"}, {"AUSTRALIE", "AU"},
        // Christmas Island
        {"CX", "CX"}, {"CXR", "CX"}, {"CHRISTMAS ISLAND", "CX"}, {"WEIHNACHTSINSEL", "CX"},
        // Cocos Islands
        {"CC", "CC"}, {"CCK", "CC"}, {"COCOS ISLANDS", "CC"},
        // Cook Islands
        {"CK", "CK"}, {"COK", "CK"}, {"COOK ISLANDS", "CK"}, {"COOKINSELN", "CK"},
        // Fiji
        {"FJ", "FJ"}, {"FJI", "FJ"}, {"FIJI", "FJ"}, {"FIDSCHI", "FJ"},
        // French Polynesia
        {"PF", "PF"}, {"PYF", "PF"}, {"FRENCH POLYNESIA", "PF"}, {"FRANZOSISCH-POLYNESIEN", "PF"},
        // Guam
        {"GU", "GU"}, {"GUM", "GU"}, {"GUAM", "GU"},
        // Kiribati
        {"KI", "KI"}, {"KIR", "KI"}, {"KIRIBATI", "KI"},
        // Marshall Islands
        {"MH", "MH"}, {"MHL", "MH"}, {"MARSHALL ISLANDS", "MH"}, {"MARSHALLINSELN", "MH"},
        // Micronesia
        {"FM", "FM"}, {"FSM", "FM"}, {"MICRONESIA", "FM"}, {"MIKRONESIEN", "FM"},
        // New Caledonia
        {"NC", "NC"}, {"NCL", "NC"}, {"NEW CALEDONIA", "NC"}, {"NEUKALEDONIEN", "NC"},
        // New Zealand
        {"NZ", "NZ"}, {"NZL", "NZ"}, {"NEW ZEALAND", "NZ"}, {"NEUSEELAND", "NZ"}, {"NOUVELLE-ZELANDE", "NZ"},
        // Nauru
        {"NR", "NR"}, {"NRU", "NR"}, {"NAURU", "NR"},
        // Niue
        {"NU", "NU"}, {"NIU", "NU"}, {"NIUE", "NU"},
        // Norfolk Island
        {"NF", "NF"}, {"NFK", "NF"}, {"NORFOLK ISLAND", "NF"}, {"NORFOLKINSEL", "NF"},
        // Samoa
        {"WS", "WS"}, {"WSM", "WS"}, {"SAMOA", "WS"},
        // Solomon Islands
        {"SB", "SB"}, {"SLB", "SB"}, {"SOLOMON ISLANDS", "SB"}, {"SALOMONEN", "SB"},
        // Tokelau
        {"TK", "TK"}, {"TKL", "TK"}, {"TOKELAU", "TK"},
        // Tonga
        {"TO", "TO"}, {"TON", "TO"}, {"TONGA", "TO"},
        // Tuvalu
        {"TV", "TV"}, {"TUV", "TV"}, {"TUVALU", "TV"},
        // Vanuatu
        {"VU", "VU"}, {"VUT", "VU"}, {"VANUATU", "VU"},
        // Wallis and Futuna
        {"WF", "WF"}, {"WLF", "WF"}, {"WALLIS AND FUTUNA", "WF"}, {"WALLIS", "WF"},

        // ── Special ─────────────────────────────────────────────────────────
        {"AQ", "AQ"}, {"ATA", "AQ"}, {"ANTARCTICA", "AQ"}, {"ANTARKTIS", "AQ"}, {"ANTARCTIQUE", "AQ"},
        {"EU", "EU"}, {"EUROPE", "EU"}, {"EUROPA", "EU"},
        {"JR", "JR"}, {"JOLLY ROGER", "JR"}, {"PIRATE", "JR"}, {"PIRATEN", "JR"}, {"PIRAT", "JR"},
        {"OLY", "OLY"}, {"OLYMPIC", "OLY"}, {"OLYMPICS", "OLY"}, {"OLYMPIA", "OLY"}, {"OLYMPISCH", "OLY"},
        {"UN", "UN"}, {"UNITED NATIONS", "UN"}, {"VEREINTE NATIONEN", "UN"},
    };
    return aliases;
}

// All valid 2-letter ISO codes (for random selection and fuzzy matching)
inline const std::vector<std::string>& getAllCountryCodes() {
    static const std::vector<std::string> codes = {
        "DZ","AO","BJ","BW","BF","BI","CM","CV","CF","TD","CD","DJ","EG","GQ","ER","ET","GA","GM","GH","GN",
        "GW","CI","KE","LS","LR","LY","MG","MW","ML","MR","MU","YT","MA","MZ","NA","NE","NG","CG","RE","RW",
        "SH","ST","SN","SC","SL","SO","ZA","SS","SD","SR","SZ","TG","TN","UG","TZ","EH","YE","ZM","ZW",
        "AI","AG","AR","AW","BS","BB","BQ","BZ","BM","BO","VG","BR","CA","KY","CL","CO","KM","CR","CU","CW",
        "DM","DO","EC","SV","FK","GF","GL","GD","GP","GT","GY","HT","HN","JM","MQ","MX","MS","NI","PA","PY",
        "PE","PR","BL","KN","LC","PM","VC","SX","TT","TC","US","VI","UY","VE",
        "AB","AF","AZ","BD","BT","BN","KH","CN","GE","HK","IN","ID","JP","KZ","LA","MO","MY","MV","MN","MM",
        "NP","KP","MP","PW","PG","PH","SG","KR","LK","TW","TJ","TH","TL","TM","VN",
        "AX","AL","AD","AM","AT","BY","BE","BA","BG","HR","CY","CZ","DK","EE","FO","FI","FR","DE","GI","GR",
        "GG","HU","IS","IE","IM","IT","JE","XK","LV","LI","LT","LU","MT","MD","MC","ME","NL","MK","NO","PL",
        "PT","RO","RU","SM","RS","SK","SI","ES","SE","CH","TR","UA","GB","VA",
        "BH","IR","IQ","IL","KW","JO","KG","LB","OM","PK","PS","QA","SA","SY","AE","UZ",
        "AS","AU","CX","CC","CK","FJ","PF","GU","KI","MH","FM","NC","NZ","NR","NU","NF","WS","SB","TK","TO",
        "TV","VU","WF",
        "AQ","EU","JR","OLY","UN",
    };
    return codes;
}

// Display names for country codes (for feedback messages)
inline const std::unordered_map<std::string, std::string>& getCountryDisplayNames() {
    static const std::unordered_map<std::string, std::string> names = {
        {"DZ","Algeria"},{"AO","Angola"},{"BJ","Benin"},{"BW","Botswana"},{"BF","Burkina Faso"},
        {"BI","Burundi"},{"CM","Cameroon"},{"CV","Cape Verde"},{"CF","Central African Rep."},{"TD","Chad"},
        {"CD","DR Congo"},{"DJ","Djibouti"},{"EG","Egypt"},{"GQ","Eq. Guinea"},{"ER","Eritrea"},
        {"ET","Ethiopia"},{"GA","Gabon"},{"GM","Gambia"},{"GH","Ghana"},{"GN","Guinea"},
        {"GW","Guinea-Bissau"},{"CI","Ivory Coast"},{"KE","Kenya"},{"LS","Lesotho"},{"LR","Liberia"},
        {"LY","Libya"},{"MG","Madagascar"},{"MW","Malawi"},{"ML","Mali"},{"MR","Mauritania"},
        {"MU","Mauritius"},{"YT","Mayotte"},{"MA","Morocco"},{"MZ","Mozambique"},{"NA","Namibia"},
        {"NE","Niger"},{"NG","Nigeria"},{"CG","Congo"},{"RE","Reunion"},{"RW","Rwanda"},
        {"SH","St Helena"},{"ST","Sao Tome"},{"SN","Senegal"},{"SC","Seychelles"},{"SL","Sierra Leone"},
        {"SO","Somalia"},{"ZA","South Africa"},{"SS","South Sudan"},{"SD","Sudan"},{"SR","Suriname"},
        {"SZ","Eswatini"},{"TG","Togo"},{"TN","Tunisia"},{"UG","Uganda"},{"TZ","Tanzania"},
        {"EH","W. Sahara"},{"YE","Yemen"},{"ZM","Zambia"},{"ZW","Zimbabwe"},
        {"AI","Anguilla"},{"AG","Antigua"},{"AR","Argentina"},{"AW","Aruba"},{"BS","Bahamas"},
        {"BB","Barbados"},{"BQ","Bonaire"},{"BZ","Belize"},{"BM","Bermuda"},{"BO","Bolivia"},
        {"VG","Brit. Virgin Is."},{"BR","Brazil"},{"CA","Canada"},{"KY","Cayman Islands"},{"CL","Chile"},
        {"CO","Colombia"},{"KM","Comoros"},{"CR","Costa Rica"},{"CU","Cuba"},{"CW","Curacao"},
        {"DM","Dominica"},{"DO","Dominican Rep."},{"EC","Ecuador"},{"SV","El Salvador"},{"FK","Falklands"},
        {"GF","French Guiana"},{"GL","Greenland"},{"GD","Grenada"},{"GP","Guadeloupe"},{"GT","Guatemala"},
        {"GY","Guyana"},{"HT","Haiti"},{"HN","Honduras"},{"JM","Jamaica"},{"MQ","Martinique"},
        {"MX","Mexico"},{"MS","Montserrat"},{"NI","Nicaragua"},{"PA","Panama"},{"PY","Paraguay"},
        {"PE","Peru"},{"PR","Puerto Rico"},{"BL","St Barthelemy"},{"KN","St Kitts"},{"LC","St Lucia"},
        {"PM","St Pierre"},{"VC","St Vincent"},{"SX","Sint Maarten"},{"TT","Trinidad"},{"TC","Turks & Caicos"},
        {"US","United States"},{"VI","US Virgin Is."},{"UY","Uruguay"},{"VE","Venezuela"},
        {"AB","Abkhazia"},{"AF","Afghanistan"},{"AZ","Azerbaijan"},{"BD","Bangladesh"},{"BT","Bhutan"},
        {"BN","Brunei"},{"KH","Cambodia"},{"CN","China"},{"GE","Georgia"},{"HK","Hong Kong"},
        {"IN","India"},{"ID","Indonesia"},{"JP","Japan"},{"KZ","Kazakhstan"},{"LA","Laos"},
        {"MO","Macau"},{"MY","Malaysia"},{"MV","Maldives"},{"MN","Mongolia"},{"MM","Myanmar"},
        {"NP","Nepal"},{"KP","North Korea"},{"MP","N. Mariana Is."},{"PW","Palau"},{"PG","Papua New Guinea"},
        {"PH","Philippines"},{"SG","Singapore"},{"KR","South Korea"},{"LK","Sri Lanka"},{"TW","Taiwan"},
        {"TJ","Tajikistan"},{"TH","Thailand"},{"TL","Timor-Leste"},{"TM","Turkmenistan"},{"VN","Vietnam"},
        {"AX","Aland"},{"AL","Albania"},{"AD","Andorra"},{"AM","Armenia"},{"AT","Austria"},
        {"BY","Belarus"},{"BE","Belgium"},{"BA","Bosnia"},{"BG","Bulgaria"},{"HR","Croatia"},
        {"CY","Cyprus"},{"CZ","Czechia"},{"DK","Denmark"},{"EE","Estonia"},{"FO","Faroe Islands"},
        {"FI","Finland"},{"FR","France"},{"DE","Germany"},{"GI","Gibraltar"},{"GR","Greece"},
        {"GG","Guernsey"},{"HU","Hungary"},{"IS","Iceland"},{"IE","Ireland"},{"IM","Isle of Man"},
        {"IT","Italy"},{"JE","Jersey"},{"XK","Kosovo"},{"LV","Latvia"},{"LI","Liechtenstein"},
        {"LT","Lithuania"},{"LU","Luxembourg"},{"MT","Malta"},{"MD","Moldova"},{"MC","Monaco"},
        {"ME","Montenegro"},{"NL","Netherlands"},{"MK","N. Macedonia"},{"NO","Norway"},{"PL","Poland"},
        {"PT","Portugal"},{"RO","Romania"},{"RU","Russia"},{"SM","San Marino"},{"RS","Serbia"},
        {"SK","Slovakia"},{"SI","Slovenia"},{"ES","Spain"},{"SE","Sweden"},{"CH","Switzerland"},
        {"TR","Turkey"},{"UA","Ukraine"},{"GB","United Kingdom"},{"VA","Vatican"},
        {"BH","Bahrain"},{"IR","Iran"},{"IQ","Iraq"},{"IL","Israel"},{"KW","Kuwait"},
        {"JO","Jordan"},{"KG","Kyrgyzstan"},{"LB","Lebanon"},{"OM","Oman"},{"PK","Pakistan"},
        {"PS","Palestine"},{"QA","Qatar"},{"SA","Saudi Arabia"},{"SY","Syria"},{"AE","UAE"},{"UZ","Uzbekistan"},
        {"AS","Am. Samoa"},{"AU","Australia"},{"CX","Christmas Is."},{"CC","Cocos Is."},{"CK","Cook Islands"},
        {"FJ","Fiji"},{"PF","Fr. Polynesia"},{"GU","Guam"},{"KI","Kiribati"},{"MH","Marshall Is."},
        {"FM","Micronesia"},{"NC","New Caledonia"},{"NZ","New Zealand"},{"NR","Nauru"},{"NU","Niue"},
        {"NF","Norfolk Is."},{"WS","Samoa"},{"SB","Solomon Is."},{"TK","Tokelau"},{"TO","Tonga"},
        {"TV","Tuvalu"},{"VU","Vanuatu"},{"WF","Wallis & Futuna"},
        {"AQ","Antarctica"},{"EU","Europe"},{"JR","Jolly Roger"},{"OLY","Olympics"},{"UN","United Nations"},
    };
    return names;
}

// ── Resolve a user input string to a 2-letter country code ───────────────────

inline std::string toUpper(const std::string& s) {
    std::string r = s;
    for (auto& c : r) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return r;
}

// Levenshtein edit distance (for fuzzy matching)
inline int editDistance(const std::string& a, const std::string& b) {
    int m = static_cast<int>(a.size());
    int n = static_cast<int>(b.size());
    if (m == 0) return n;
    if (n == 0) return m;

    std::vector<int> prev(n + 1), curr(n + 1);
    for (int j = 0; j <= n; ++j) prev[j] = j;

    for (int i = 1; i <= m; ++i) {
        curr[0] = i;
        for (int j = 1; j <= n; ++j) {
            int cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
            curr[j] = std::min({prev[j] + 1, curr[j - 1] + 1, prev[j - 1] + cost});
        }
        std::swap(prev, curr);
    }
    return prev[n];
}

// Decode a flag emoji (Regional Indicator Symbol pair) to a 2-letter ISO code.
// Flag emojis are encoded as two consecutive U+1F1E6..U+1F1FF code points in UTF-8.
// Each Regional Indicator is 4 bytes in UTF-8: F0 9F 87 A6..BF
// Returns empty string if not a valid flag emoji.
inline std::string flagEmojiToCode(const std::string& input) {
    // A Regional Indicator in UTF-8 is 4 bytes: 0xF0 0x9F 0x87 [0xA6..0xBF]
    // A flag emoji is exactly 2 of these = 8 bytes
    std::string trimmed = input;
    // Strip leading/trailing whitespace
    while (!trimmed.empty() && (trimmed.front() == ' ' || trimmed.front() == '\t')) trimmed.erase(trimmed.begin());
    while (!trimmed.empty() && (trimmed.back() == ' ' || trimmed.back() == '\t')) trimmed.pop_back();

    if (trimmed.size() < 8) return "";

    auto byte = [](char c) -> unsigned char { return static_cast<unsigned char>(c); };

    // Check first Regional Indicator (bytes 0-3)
    if (byte(trimmed[0]) != 0xF0 || byte(trimmed[1]) != 0x9F || byte(trimmed[2]) != 0x87) return "";
    unsigned char b3 = byte(trimmed[3]);
    if (b3 < 0xA6 || b3 > 0xBF) return "";
    char c1 = static_cast<char>('A' + (b3 - 0xA6));

    // Check second Regional Indicator (bytes 4-7)
    if (byte(trimmed[4]) != 0xF0 || byte(trimmed[5]) != 0x9F || byte(trimmed[6]) != 0x87) return "";
    unsigned char b7 = byte(trimmed[7]);
    if (b7 < 0xA6 || b7 > 0xBF) return "";
    char c2 = static_cast<char>('A' + (b7 - 0xA6));

    std::string code;
    code += c1;
    code += c2;
    return code;
}

// Resolve user input to 2-letter ISO code
// Returns empty string if no match found (caller should pick random)
inline std::string resolveCountryCode(const std::string& input) {
    if (input.empty() || input == "?") return "";

    // 0. Flag emoji (e.g. 🇩🇪 → DE)
    std::string emojiCode = flagEmojiToCode(input);
    if (!emojiCode.empty()) {
        // Verify it's a known country code
        const auto& aliases = getCountryAliases();
        if (aliases.count(emojiCode)) return aliases.at(emojiCode);
    }

    std::string upper = toUpper(input);

    // 1. Exact alias match
    const auto& aliases = getCountryAliases();
    auto it = aliases.find(upper);
    if (it != aliases.end()) return it->second;

    // 2. Substring match: check if input is contained in any alias key or vice versa
    //    (e.g., "deutsch" matches "DEUTSCHLAND" → DE)
    std::string bestSubMatch;
    int bestSubLen = 999;
    for (const auto& [key, code] : aliases) {
        if (key.size() < 4) continue; // skip short codes for substring match
        if (key.find(upper) != std::string::npos || upper.find(key) != std::string::npos) {
            // Prefer shorter keys (more specific match)
            int len = static_cast<int>(key.size());
            if (bestSubMatch.empty() || len < bestSubLen) {
                bestSubMatch = code;
                bestSubLen = len;
            }
        }
    }
    if (!bestSubMatch.empty()) return bestSubMatch;

    // 3. Fuzzy match using edit distance (only for inputs >= 3 chars)
    if (upper.size() >= 3) {
        std::string bestCode;
        int bestDist = 999;
        int maxDist = std::max(2, static_cast<int>(upper.size()) / 3);

        for (const auto& [key, code] : aliases) {
            if (key.size() < 3) continue; // skip 2-letter codes for fuzzy
            int dist = editDistance(upper, key);
            if (dist < bestDist && dist <= maxDist) {
                bestDist = dist;
                bestCode = code;
            }
        }
        if (!bestCode.empty()) return bestCode;
    }

    return ""; // no match
}

// Pick a random country code
inline std::string randomCountryCode(std::mt19937& rng) {
    const auto& codes = getAllCountryCodes();
    std::uniform_int_distribution<int> dist(0, static_cast<int>(codes.size()) - 1);
    return codes[dist(rng)];
}

} // namespace is::games::country_elimination
