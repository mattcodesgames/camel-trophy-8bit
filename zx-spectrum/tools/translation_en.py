"""English translation of Camel Trophy (Aquarius Edizioni / Digiteam).

STRINGS[part]       BASIC string literal (as srepr) -> English. Control codes are {xx}.
                    Strings printed at fixed screen positions keep their padding so they stay centred.
LINE_PATCHES[part]  {line: [(old, new)]} raw edits of a line (srepr form) for logic that depends on
                    the language: the yes key S(i) becomes Y, and word order in a few labels.
MC_BLOCKS           part 3 machine-code messages: (address, [new text per run]); None keeps a run.
                    Each run keeps its original length, so every line must fit the Italian one.
CAPTIONS            text drawn as pixels inside pictures.
"""

def _tape_prompt(ink, text):
    """The 'start the tape' banner: 3 screen rows of 32, text centred on the middle row."""
    pad = ' ' * 34
    return '{11}{06}' + pad + '{10}{' + ink + '}' + text + pad


QUIZ = {
    # --- question 1 ---
    "Dopo aver raddrizzato la barra di accoppiamento dello sterzo, precedente- mente storta, come procedete per ri- portare la convergenza delle ruote entro limiti accettabili ?":
        "After straightening the bent steering track rod, how do you bring the wheel alignment back within acceptable limits?",
    "Si procede ad una regolazione ad occhio delle ruote;": "You adjust the wheels by eye;",
    "Si percorre qualche metro per vedere da che parte tira la vettura;": "You drive a few metres to see which way the vehicle pulls;",
    "Si confronta la convergenza con quella delle ruote posteriori.": "You compare the alignment with that of the rear wheels.",
    # --- 2 ---
    "Quale semplice operazione vi consente di stabilire quale cilindro non funziona in un motore Diesel ?":
        "What simple test tells you which cylinder is not working in a Diesel engine?",
    "Si controlla un cilindro per volta smontando la relativa candela;": "Check one cylinder at a time by removing its spark plug;",
    "Come sopra, ma smontando gli iniettori anziche' le candele;": "As above, but removing the injectors instead of the plugs;",
    "Smontando sia le candele che gli iniettori.": "Remove both the plugs and the injectors.",
    # --- 3 ---
    "Dovendo superare un passaggio la cui pendenza laterale supera di un grado l'angolo teorico di ribaltamento del vostro 4x4, dopo aver ascoltato i consigli dei vostri compagni di viaggio, come procedete ?":
        "You must cross a stretch whose side slope is one degree past the theoretical rollover angle of your 4x4. After hearing your crew's advice, what do you do?",
    "Faccio scendere i compagni di viaggio;": "I get my crew out of the car;",
    "Li sposto all'interno dell'auto per spostare il baricentro;": "I move them around inside the car to shift the centre of gravity;",
    "Li faccio scendere e spingere la macchina dalla parte verso cui e' inclinata.": "I get them out to push the car on the side it leans towards.",
    # --- 4 ---
    "Siete da soli e irrimediabilmente impan- tanati. Disponete di una pala e del ver- ricello, ma non esistono punti dove fis- sare adeguatamente il cavo del ver- ricello. Come potete uscire da tale si- tuazione usando solo pala e verricello ?":
        "You are alone and hopelessly bogged down. You have a shovel and the winch, but nowhere to anchor the winch cable properly. How can you get out using only the shovel and the winch?",
    "Pianto la pala con angolazione tale da permettere un appiglio al cavo del verricello;": "I drive the shovel in at an angle that gives the winch cable a hold;",
    "Metto la pala sotto le ruote dopo averle sgonfiate in parte;": "I put the shovel under the wheels after letting them down a little;",
    "Con la pala scavo intorno alle ruote per disimpantanarmi.": "I dig round the wheels with the shovel to free myself.",
    # --- 5 ---
    "State procedendo su una distesa pianeggiante con vegetazione folta ma molto bassa. Da cosa distinguete le zone umide e quindi pericolose, da quelle sec- che e quindi piu' sicure ?":
        "You are crossing a flat plain with thick but very low vegetation. How do you tell the wet, and so dangerous, ground from the dry, and so safer, ground?",
    "Dal tipo di piante presenti;": "By the kind of plants growing;",
    "Dalla presenza eventuale di fiori ed insetti;": "By any flowers and insects present;",
    "Dal colore della vegetazione.": "By the colour of the vegetation.",
    # --- 6 ---
    "Avendo il terzo differenziale non bloccato, su di una vettura a quattro ruote motrici sempre in presa, nella peggiore condizione di aderenza, quante ruote mantengono coppia motrice ?":
        "On a permanent four-wheel-drive vehicle with the centre differential unlocked, in the worst grip conditions, how many wheels keep drive torque?",
    "Due ruote;": "Two wheels;",
    "Tre ruote;": "Three wheels;",
    "Nessuna ruota.": "No wheels.",
    # --- 7 ---
    "A quale regime (giri/minuto) il motore esprime la sua massima capacita' lavora- tiva con il minor consumo di energia ?":
        "At what engine speed (rpm) does the engine do the most work for the least energy?",
    "Subito prima del fuorigiri;": "Just before over-revving;",
    "In coppia;": "At peak torque;",
    "Quando le ruote slittano.": "When the wheels slip.",
    # --- 8 ---
    "Dovendo viaggiare su terreni sabbio- si, e' consigliabile dotare il vostro 4x4 con pneumatici radiali ? Se si, perche' ?":
        "For driving on sandy ground, is it wise to fit radial tyres to your 4x4? If so, why?",
    "Si, perche' favoriscono una presa migliore sulla sabbia;": "Yes, because they grip the sand better;",
    "No, perche' si consumano piu' facilmente;": "No, because they wear out more easily;",
    "E' indifferente.": "It makes no difference.",
    # --- 9 ---
    "Se dopo una lunga serie di guadi vi accorgete che e' entrata acqua nei dif- ferenziali e non avete olio per proce- dere alla sostituzione dello stesso, quale operazione vi consentira' di con- tinuare il viaggio con sufficienti garanzie ?":
        "After a long run of fords you find water has got into the differentials and you have no oil to change it. What will let you carry on with reasonable safety?",
    "Tappare il differenziale con uno straccio;": "Plug the differential with a rag;",
    "Svuotare del tutto il differenziale;": "Drain the differential completely;",
    "Riempire completamente il diffe- renziale con acqua.": "Fill the differential right up with water.",
    # --- 10 ---
    "Siete su una ripida salita e le ruote cominciano a pattinare. Il veicolo si arresta. Come fate a superare questo imprevisto in tutta sicurezza ?":
        "You are on a steep climb and the wheels start to spin. The vehicle stops. How do you deal with this safely?",
    "Spingere contemporanemente fre- no e frizione e lasciarsi calare in basso modulando la frenata;":
        "Press brake and clutch together and roll back down, easing the brake;",
    "Spingere contemporaneamente fre- no e frizione; inserire la retro, rilasciare i pedali e lasciarsi ca- lare solo con il freno motore;":
        "Press brake and clutch together, select reverse, release the pedals and roll back on engine braking alone;",
    "Spingere velocemente il pedale della frizione, ancora piu' veloce- mente cambiare marcia e superare di potenza il tratto a tutto gas.":
        "Dip the clutch quickly, change gear even faster and power up the slope at full throttle.",
    # --- 11 ---
    "Avete appena effettuato un profondo guado: quale manovra dovete compiere per garantire la vostra e la altrui sicurezza ?":
        "You have just crossed a deep ford: what must you do for your own and other people's safety?",
    "Controllare che non sia rimasta acqua nel tubo di scappamento;": "Check that no water is left in the exhaust pipe;",
    "Controllare che non ci siano state infiltrazioni nel serbatoio del gasolio;": "Check that no water has got into the diesel tank;",
    "Controllare l'impianto frenante.": "Check the brakes.",
    # --- 12 ---
    "Quale dei seguenti accessori e' scon- sigliato ?": "Which of these accessories is not recommended?",
    "Tanica acqua;": "Water jerrycan;",
    "Guanti da lavoro;": "Work gloves;",
    "Pala;": "Shovel;",
    "Ascia;": "Axe;",
    "Paranco;": "Hoist;",
    "Cavo di traino metallico.": "Steel tow cable.",
    # --- 13 ---
    "Dovendo affrontare un guado a noi sconosciuto, quale precauzione dovrem- mo prendere ?":
        "Facing a ford we don't know, what precaution should we take?",
    "Verificare la perfetta chiusura di portiere e finestrini;": "Make sure the doors and windows are tightly shut;",
    "Controllare la profondita' del guado e lo stato del fondo prima di attraversarlo;": "Check the depth of the ford and the state of its bed before crossing;",
    "Predisporre una fune di sicurezza cui agganciare il veicolo.": "Rig a safety rope to tie the vehicle to.",
    # --- 14 ---
    "Si rompe il differenziale posteriore del nostro Land Rover 90. Ritenete sia possibile proseguire la marcia ? Se si, quali operazioni dovete effettuare ?":
        "The rear differential of our Land Rover 90 breaks. Can you carry on? If so, what must you do?",
    "Si, inserendo la trazione inte- grale;": "Yes, by engaging four-wheel drive;",
    "Si, disinserendo il differenziale centrale;": "Yes, by disengaging the centre differential;",
    "No, perche' pregiudicherebbe l'in- tegrita' della vettura.": "No, because it would damage the vehicle.",
    # --- 15 ---
    "Se durante una lunga e ripida discesa su terreno a scarsa aderenza il veicolo sbanda con il retrotreno verso destra, quale manovra ritenete corretta ?":
        "On a long, steep descent with poor grip, the back of the vehicle slides to the right. Which action is correct?",
    "Sterzate a destra, dolcemente, ed accelerate;": "Steer gently to the right and accelerate;",
    "Frenate ed inserite una marcia piu' bassa;": "Brake and select a lower gear;",
    "Sterzate a sinistra ed accelerate;": "Steer to the left and accelerate;",
    "Sterzate a sinistra ed inserendo una marcia piu' alta, accelerate in modo progressivo.":
        "Steer to the left, select a higher gear and accelerate gradually.",
    # --- 16 ---
    "State percorrendo un sentiero alpino in terra battuta, e un tronco d'albero vi impedisce di proseguire. Cosa fate ?":
        "You are on a dirt track in the mountains and a tree trunk blocks your way. What do you do?",
    "Spostate il tronco;": "Move the trunk;",
    "Aggirate l'ostacolo affrontando una ripida salita, tecnicamente fat- tibile, su un bel prato erboso;":
        "Go round it by a steep but passable climb over a lovely grassy meadow;",
    "Passate con la Land sopra il tronco.": "Drive the Land Rover over the trunk.",
    # --- 17 ---
    "Quando il sentiero che percorrete presenta profonde carreggiate, quale comportamento tenete ?":
        "When the track you are on has deep ruts, what do you do?",
    "Entrate con tutte le ruote nella carreggiate, seguendole pedissequa- mente;": "Put all the wheels in the ruts and follow them exactly;",
    "Entrate con due ruote nelle stesse cercando di mantenere le altre due al di fuori;": "Put two wheels in the ruts and try to keep the other two out;",
    "Cercate il piu' possibile di man- tenere tutte le ruote al di fuori delle carreggiate.": "Try as far as you can to keep all the wheels out of the ruts.",
    # --- 18 ---
    "Su una carta topografica al 50.000, qual'e' la differenza altimetrica fra le isoipse contigue espresse in metri ?":
        "On a 1:50,000 map, what is the height difference in metres between neighbouring contour lines?",
    "Cinquanta metri;": "Fifty metres;",
    "Cento metri;": "A hundred metres;",
    "Cinquecento metri.": "Five hundred metres.",
    # --- 19 ---
    "Ritenete sia possibile effettuare il punto con una normale bussola da rilevamento stando seduti all'interno del veicolo ?":
        "Can you take a bearing with an ordinary sighting compass while sitting inside the vehicle?",
    "No, perche' la bussola risente dei metalli della macchina;": "No, because the metal of the car affects the compass;",
    "Si, purche' la bussola non sia vicina a magneti.": "Yes, as long as the compass is not near any magnets.",
    # --- 20 ---
    "Disponendo di un veicolo 4x4 dotato di un motore molto potente, l'inserimento del riduttore cosa comporta ?":
        "In a 4x4 with a very powerful engine, what does engaging low range do?",
    "Un aumento rilevante della ve- locita' massima;": "It greatly increases the top speed;",
    "Una riduzione della potenza del motore;": "It reduces the engine's power;",
    "La possibilita' di trasmettere alle ruote motrici la massima potenza esprimibile.": "It lets the driven wheels receive the most power the engine can give.",
    # --- 21 ---
    "Perche' e' sconsigliabile marciare su strada asfaltata asciutta con un 4x4 non dotato di terzo differenziale, avendo inserita la trazione integrale ?":
        "Why is it unwise to drive on dry tarmac in four-wheel drive with a 4x4 that has no centre differential?",
    "Perche' si rovinano le gomme;": "Because it ruins the tyres;",
    "Perche' si forza troppo il motore;": "Because it strains the engine too much;",
    "Perche' c'e' scarsa tenuta di strada.": "Because the road holding is poor.",
    # --- 22 ---
    "Dovendo ripartire su un terreno a scarsa aderenza, quale marcia usereste e perche' ?":
        "Moving off again on ground with poor grip, which gear would you use and why?",
    "Una marcia alta per scaricare il piu' possibile la potenza del motore;": "A high gear, to tame the engine's power as much as possible;",
    "Una marcia bassa per disporre della maggior potenza possibile;": "A low gear, to have as much power as possible;",
    "La marcia piu' bassa giocando col gas e la frizione.": "The lowest gear, working the throttle and clutch.",
    # --- 23 ---
    "Il luogo piu' idoneo per costruire un riparo temporaneo e':": "The best place to build a temporary shelter is:",
    "In una grotta;": "In a cave;",
    "Sul greto prosciugato di un fiume o torrente;": "On the dry bed of a river or stream;",
    "In uno spazio aperto ed elevato;": "In an open, high place;",
    "Vicino ad una fonte d'acqua dolce anche se in zona paludosa.": "Near fresh water, even in a marshy area.",
    # --- 24 ---
    "E' possibile bere acqua fangosa e stagnante facendola bollire per 1 minuto e lasciandola decantare per 12 ore. Non disponendo di cosi' tanto tempo...":
        "Muddy, stagnant water can be drunk after boiling it for 1 minute and letting it settle for 12 hours. If you do not have that much time...",
    "Si filtra l'acqua bollita con una garza ripiegata piu' volte;": "Filter the boiled water through gauze folded several times;",
    "Si filtra l'acqua bollita attraverso una lunga canna riempita di sabbia;": "Filter the boiled water through a long cane filled with sand;",
    "Si fa bollire l'acqua per un'ora anziche' per un minuto;": "Boil the water for an hour instead of a minute;",
    "Si evita di bere quell'acqua.": "Avoid drinking that water.",
    # --- 25 ---
    "Dovendo usare il machete per aprirsi un varco nella giungla, si assestano colpi brevi e secchi...":
        "When using a machete to clear a way through the jungle, you strike short, sharp blows...",
    "Dall'alto verso il basso;": "From top to bottom;",
    "Dal basso verso l'alto;": "From bottom to top;",
    "Diagonalmente.": "Diagonally.",
    # --- 26 ---
    "Dovendo marciare di notte nella giungla, una delle poche possibilita' per ridurre i tempi di trasferimento e' quella di servirsi dei sentieri tracciati dagli animali. E' consigliabile ?":
        "Travelling through the jungle at night, one of the few ways to save time is to use trails made by animals. Is this advisable?",
    "Si, ma dipende dall'animale che ha tracciato il sentiero;": "Yes, but it depends on which animal made the trail;",
    "No, perche' sono anche seguiti dai predatori.": "No, because predators use them too.",
    # --- 27 ---
    "Quale scopo ci prefiggiamo adagiando un paziente in posizione anti-shock ?":
        "What is the aim of laying a patient in the anti-shock position?",
    "Di migliorare l'approvvigionamento di sangue agli organi di importanza vitale;": "To improve the blood supply to the vital organs;",
    "Di proteggere la ferita;": "To protect the wound;",
    "Di rilassare gli organi interni;": "To relax the internal organs;",
    "Di rilassare i vasi sanguigni delle gambe e delle braccia.": "To relax the blood vessels of the legs and arms.",
    # --- 28 ---
    "Quando deve il soccorritore applicare il laccio emostatico ?": "When should a first-aider apply a tourniquet?",
    "In caso di forte emorragia ar- teriosa;": "For heavy arterial bleeding;",
    "Quando tutte le altre misure dell'emostasi non hanno dato esito positivo;": "When every other way of stopping the bleeding has failed;",
    "In tutte le emorragie.": "For all bleeding.",
    # --- 29 ---
    "Quale di queste situazioni e' la piu' pericolosa ?": "Which of these situations is the most dangerous?",
    "Perdita di coscienza;": "Loss of consciousness;",
    "Frattura aperta;": "Open fracture;",
    "Frattura completa con ferita;": "Complete fracture with a wound;",
    "Ferita lacerata.": "Torn wound.",
    # --- 30 ---
    "Dovendo trasmettere il segnale di s.o.s., qual'e' tra quelle indicate qui sotto, l'esatta sequenza dei segni?":
        "To send an S.O.S. signal, which of the sequences below is correct?",
    # --- 31 ---
    "Qual'e' il significato del segnale terra-aria <LL> ?": "What does the ground-to-air signal <LL> mean?",
    "Qui si puo' atterrare;": "You can land here;",
    "Occorre un medico;": "A doctor is needed;",
    "Tutto bene.": "All is well.",
    # --- 32 ---
    "Quali sono i sintomi della malaria ?": "What are the symptoms of malaria?",
    "Gonfiori e dolori ghiandolari, arrossamenti cutanei, dolori al capo, alla schiena ed ai muscoli;":
        "Swollen, painful glands, skin rashes, pain in the head, back and muscles;",
    "Brividi, cefalea, forti e rapide escursioni di temperatura, tume- fazioni e dolori ghiandolari, dissen- teria e vomito;":
        "Shivering, headache, big and rapid swings in temperature, swollen and painful glands, dysentery and vomiting;",
    "Bruciori e disturbi intestinali, perdita del senso dell'equilibrio, forte febbre continua, svenimenti.":
        "Burning and bowel upsets, loss of balance, constant high fever, fainting.",
}

COMMON = {
    "{10}{04}         Premi un tasto": "{10}{04}          Press any key",
    "{10}{04}  premi <ENTER> per confermare  ": "{10}{04}    press <ENTER> to confirm    ",
    "{11}{06}{10}{00}    {92}{93}    CAMEL TROPHY    {90}{91}    ": None,
    "{10}{06}________": None,
}

STRINGS = {
    1: {
        **QUIZ,
        "{10}{04}SELEZIONI EQUIPAGGIO{10}{06}": "{10}{04}   CREW SELECTION   {10}{06}",
        "Teoria e Meccanica Fuoristrada": "Off-Road Theory and Mechanics",
        "Tecniche  di Sopravvivenza": "   Survival Techniques",
        "Orientamento": " Navigation",
        "Nozioni di Pronto Soccorso": "   First Aid Knowledge",
        "Il codice che ti e' stato assegnato e':": "The code you have been given is:",
        "Inserisci per favore i tuoi dati:": "Please enter your details:",
        "{10}{04}Nome: ": "{10}{04}Name: ",
        "{10}{04}Cognome: ": "{10}{04}Surname: ",
        "Sei stato scelto per partecipare al Camel Trophy di quest' anno, ma prima vogliamo sapere come te la cavi con i motori e con il fuoristrada in generale.":
            "You have been chosen to take part in this year's Camel Trophy, but first we want to know how you get on with engines and off-road driving in general.",
        "Rispondi alle domande premendo il nu- mero della risposte che riterrai giuste.":
            "Answer the questions by pressing the number of the answer you think is right.",
        "Dalle risposte date verranno calcolati dei punteggi che influiranno succes- sivamente sullo svolgimento del Trophy.":
            "Your answers will be scored, and the scores will affect how the Trophy goes later on.",
        "{10}{04}         Premi un tasto": COMMON["{10}{04}         Premi un tasto"],
        _tape_prompt('00', 'FAI PARTIRE IL  REGISTRATORE'): _tape_prompt('00', 'START THE TAPE'.center(28)),
        "{10}{04}       scegli la risposta": "{10}{04}       choose your answer",
        "{10}{04}Risp. ": "{10}{04}Ans.  ",
        "{10}{04}  premi <ENTER> per confermare  ": COMMON["{10}{04}  premi <ENTER> per confermare  "],
        "QUALIFICAZIONI": "  QUALIFYING  ",
        "__________________{10}{04}domanda{10}{06} ______": "_________________{10}{04}question{10}{06} ______",
    },
    2: {
        "Passaggio guado": "Passing a ford",
        "Orientamento": "Orienteering",
        "Costruzione ponte": "Constructing a bridge",
        "Superamento dosso": "Surmounting a hump",
        "{10}{04}SELEZIONE PERCORSO{10}{06}": "{10}{04} ROUTE SELECTION  {10}{06}",
        "{10}{04}2{10}{06}  Amazzonia": "{10}{04}2{10}{06}  Amazon",
        "Miglia": "Miles",
        "Arrivo:": "ETA:",
        "Prove Speciali:": "Special Stages:",
        "si": "yes",
        "FINE DEL TROPHY": "END OF THE TROPHY",
        "FINE TROPHY": "END OF THE TROPHY",
        "La lunghezza totale del percorso Zaire e' di 1400 miglia ma il livello di difficolta' e' minimo.":
            "The Zaire route is 1400 miles long in all, but it is the easiest.",
        "Se vuoi svolgere il tuo Camel Trophy nel cuore dell'Africa, premi S; un altro tasto per tornare al menu.":
            "If you want to run your Camel Trophy in the heart of Africa, press Y; any other key returns to the menu.",
        "Il Camel Trophy ambientato nella foresta amazzonica e' lungo 1300 miglia da Itaituba a Rio Branco e si svolge in piena stagione delle pioggie.":
            "The Camel Trophy in the Amazon rainforest runs 1300 miles from Itaituba to Rio Branco, in the middle of the rainy season.",
        "Se hai raggiunto un buon grado di preparazione e ti senti di sfidare il polmone verde del nostro pianeta, premi S; per tornare al menu, un altro tasto.":
            "If you are well prepared and feel ready to take on the green lung of our planet, press Y; any other key returns to the menu.",
        "Il percorso piu' breve non sempre e' il piu' agevole: e le 1000 miglia di continua sfida alla natura del misterioso Borneo ne sono la prova.":
            "The shortest route is not always the easiest: 1000 miles of constant struggle with the nature of mysterious Borneo prove it.",
        "Accetta l'avventura premendo S; un altro tasto invece per tornare al menu.":
            "Accept the adventure by pressing Y; any other key returns to the menu.",
        "Se non ti senti ancora sufficientemente preparato ad affrontare un regolare Camel Trophy e preferisci fare pratica delle varie prove speciali, o se vuoi progettare il tuo percorso ad hoc, puoi utilizzare l'opzione Do It Yourself.":
            "If you don't yet feel ready for a full Camel Trophy and would rather practise the special stages, or you want to design your own route, use the Do It Yourself option.",
        "I seimila acri del parco di Eastnor Castle, Birmingham, sono a tua dispo- sizione con dossi, acquitrini e boschi, per permetterti una simulazione all'al- tezza del Camel Trophy.":
            "The six thousand acres of Eastnor Castle park, Birmingham, are yours, with humps, marshes and woods, for a simulation worthy of the Camel Trophy.",
        "Il meccanismo con cui funziona il D.I.Y. e' semplice: devi organizzare i dieci giorni di Trophy suddividendo la quan- tita' di prove speciali indicate piu' avanti in ragione di due per tappa.":
            "D.I.Y. is simple: you plan the ten days of the Trophy by sharing out the special stages listed next, two for each day.",
        "Gli elementi fissi di ogni giornata sono: controllo della parte meccanica della Land e due fasi di trasferimento, la seconda delle quali puo' essere effet- tuata in Fuel Economy Run (F.E.R.).":
            "Every day always has a mechanical check of the Land Rover and two transfer legs, the second of which can be run as a Fuel Economy Run (F.E.R.).",
        "Fuel Economy Run (s/n)?": "Fuel Economy Run (y/n)?",
        "Il progetto del D.I.Y. e' terminato. Se sei contento del percorso che hai creato e vuoi usarlo per la gara, premi S; con un altro tasto torni al menu.":
            "Your D.I.Y. plan is finished. If you are happy with the route you made and want to race it, press Y; any other key returns to the menu.",
        "{10}{04} Scegli la prova che preferisci": "{10}{04} Choose the stage you want",
        "       {10}{04}Fai la tua scelta{10}{07}": "        {10}{04}Make your choice{10}{07}",
        "{10}{04}  premi <ENTER> per confermare  ": COMMON["{10}{04}  premi <ENTER> per confermare  "],
        "{10}{04}         Premi un tasto": COMMON["{10}{04}         Premi un tasto"],
        "Il percorso scelto verra' automa- ticamente trasferito al blocco succes- sivo; se pero' vuoi memorizzare i dati per i tuoi futuri Camel Trophy senza dover caricare ogni volta i primi due pro- grammi, prepara il registratore e premi il tasto <S>.":
            "Your route will be passed on to the next part automatically. To keep it for future Camel Trophies without loading the first two programs every time, get a tape ready and press <Y>.",
        "Un altro tasto per continuare.": "Any other key to continue.",
        _tape_prompt('04', 'FAI PARTIRE IL  REGISTRATORE'): _tape_prompt('04', 'START THE TAPE'.center(28)),
        "{10}{04}       scegli il percorso": "{10}{04}        choose your route",
    },
}
for _part in STRINGS.values():
    for _k in [k for k, v in _part.items() if v is None]:
        del _part[_k]

LINE_PATCHES = {
    2: {
        # "3' Tappa:" -> "Stage 3:"
        60: [("{f1}a$=a$+\" ' Tappa:\"", "{f1}a$=\"Stage \"+a$+\":\"")],
        # "1' Giorno" -> "Day 1"
        1430: [("d;\"' Giorno\"", "\"Day \";d")],
        # yes = Y instead of S(i); the special-stage key S (Surmounting a hump) is unchanged
        1436: [('"S"', '"Y"')],
        3002: [('"S"', '"Y"')],
        6402: [('"S"', '"Y"')],
        # the Borneo map moves 8 bytes earlier to make room for its English labels (see MAPS)
        900: [(',165{0e}{00}{00}{a5}{00}{00}:', ',157{0e}{00}{00}{9d}{00}{00}:')],
    },
}

# part 3: (address of first control code, [replacement per text run]); None keeps the run
MC_BLOCKS = [
    (0x99E9, [" YOU HAVE RUN OUT OF FUEL "]),
    (0xBFB5, [" YOU TACKLED THE FORD BADLY ", "AND GOT BOGGED DOWN"]),
    (0xC1D7, ["  YOU WERE TOO  ", "SLOW FOR THE", "  HUMP  "]),
    (0xC210, ["YOU CAN'T TAKE ", " A HUMP ", "  AT THIS SPEED!"]),
    (0xD4D6, ["  YOU USED TOO MUCH DIESEL  ", " IN THIS F.E.R. STAGE "]),
    (0xD607, ["Welcome to", None, "CHOOSE:", "1-Keyboard", None, "3-Load Route"]),
    (0xD710, ["NONE", "PASSING A FORD", "ORIENTEERING", "BUILDING A BRIDGE", None, "CLIMBING A HUMP"]),
    (0xD787, [" Camel Trophy - Day ", "Itinerary:", "MILES", "GALS. FUEL", "SPECIAL STAGES:", None, "MILES", None, "MILES",
              "TRANSFER        MILES", "SCORES:         BONUS", "PENALTIES", None, "  AMAZON", None, None]),
    (0xDA0C, ["DAWN: IT'S TIME TO GET", "UP AND FACE THE NEW", "TROPHY DAY.", "FIRST YOU MUST CHECK THE",
              "MECHANICS OF YOUR LAND: YOU", "MUST REBUILD THE CAR, CHOOSING", "PARTS FROM THE PILE ON THE",
              "LEFT OF THE SCREEN WITH KEY", "<9> AND PICKING THEM UP WITH", "KEY <0>. A WRONG CHOICE CAN",
              "BE CORRECTED WITH <9>.", "NOTE: PARTS CAN ONLY BE", "FITTED IN A CERTAIN AREA OF", "THE SCREEN.", "",
              "TIP: PUT THE STRIPES ON THE", "TOP..."]),
    (0xDBF2, ["FORDS ARE HARD TO CROSS WHEN", "THE WATER REACHES THE WINDOWS", "AND THE MUD GRIPS THE WHEELS", "..."]),
    (0xDC62, ["THE TROPHY TRACKS ARE FULL OF", "OBSTACLES TO AVOID WITH QUICK", "REFLEXES AND DEFT STEERING", ""]),
    (0xDCCF, ["TAKE THE BRIDGE PIECES TO", "THE RIVER, FINDING THE", "RIGHT SPOT IN THE MEADOW BY",
              "THE LAND. AVOID THE SNAKES,", "MIND THE TIME. GOOD LUCK!"]),
    (0xDD71, ["IN THE UNKNOWN JUNGLE YOU ALSO", "NEED LUCK TO DECIDE WHICH", "WAY TO TURN AT A SUDDEN", "FORK!"]),
    (0xDDDB, ["TAKE THE HUMP AT THE RIGHT", "SPEED AND IN A SUITABLE", "GEAR."]),
    (0xDE2B, ["THE DAY IS NEARLY OVER.", "REACH THE NEXT BASE CAMP", "WHILE KEEPING YOUR FUEL", "USE UNDER CONTROL."]),
    (0xDFE1, ["EVALUATION CARD FOR:", None, None, "FOR THE ROUTE:", None, "TOTAL BONUS:", "TOTAL PENALTIES:",
              "THEORY TEST CODE:", None, "1) New Adventure", "2) Save Final Card"]),
]

# ((part, screen base, y0, y1, x_lo, x_hi, original text, pitch), English)
CAPTIONS = [
    ((1, 0x4000, 179, 189, 32, 223, 'CARICAMENTO CAMEL TROPHY', 8), 'LOADING CAMEL TROPHY'),
    ((1, 0x4000, 96, 103, 60, 202, 'PROGETTO SOFTWARE BY', 7), 'SOFTWARE BY'),
    ((1, 0xBEE2, 179, 189, 18, 238, 'QUALIFICAZIONI CAMEL TROPHY', 8), 'CAMEL TROPHY QUALIFICATION'),
    ((2, 0x4000, 179, 189, 8, 255, 'CARICAMENTO SELEZIONE PERCORSO', 8), 'LOADING ROUTE SELECTION'),
]

# letters the Italian captions never use, drawn in the style of each font
EXTRA_GLYPHS = {
    (1, 0x4000, 179): {
        'D': ['........', '######..', '#######.', '.#....#.', '.#....#.', '.#....#.', '.#....#.', '.#####..', '##..##..', '........'],
        'G': ['........', '#######.', '#######.', '.#......', '.#......', '.#..###.', '.#....#.', '.######.', '##...##.', '........'],
    },
    (2, 0x4000, 179): {
        'D': ['........', '........', '.####...', '.....#..', '.##...#.', '.##...#.', '.##...#.', '.#####..', '.####...', '........'],
        'G': ['........', '........', '..####..', '......#.', '.##.....', '.##.###.', '.##...#.', '.######.', '..####..', '........'],
        'U': ['........', '........', '.#....#.', '......#.', '.##...#.', '.##...#.', '.##...#.', '.######.', '..####..', '........'],
    },
}

# part 2 route maps: run-length encoded pictures at these addresses, with an 8-line info panel
MAP_LINE_Y0 = 6                   # first panel line band is y 6..17, then every 16 px
MAP_LABEL_X = (144, 186)          # label column (the colon at x=188 stays)
MAP_VALUE_X = (192, 253)
MAP_LABELS_IT = ['percorso', 'partenza', 'arrivo', 'lunghezza', 'giorni', 'vettura', 'clima', 'strade']
MAP_LABELS_EN = ['route', 'start', 'end', 'length', 'days', 'car', 'climate', 'roads']
MAPS = {
    0x9FEC: {
        'it': ['zaire', 'kisangan', 'kinshasa', '1400 miglia', '10', 'land rover', 'caldo-umido', 'piste locali'],
        'en': [None, None, None, '1400 miles', None, None, 'hot-humid', 'local tracks'],
    },
    0xB27E: {
        'it': ['amazzonia', 'itaituba', 'rio branco', '1300 miglia', '10', 'land 110', 'tropicale', 'piste locali'],
        'en': ['amazon', None, None, '1300 miles', None, None, 'tropical', 'local tracks'],
        'labels': [((16, 60, 22, 34), 'brasile', 'brazil')],
        # (y0, y1, x_lo, x_hi, text, pitch), English, frame rows to close up
        'captions': [((184, 191, 32, 103, 'Amazzonia', 8), 'Amazon', (184, 191))],
        'end': 0xC59D,
    },
    0xC5A5: {
        'it': ['borneo', 'balikpapan', 'samarinda', '1000 miglia', '10', 'land 90', 'equatoriale', 'jungla'],
        'en': [None, None, None, '1000 miles', None, None, 'equatorial', 'jungle'],
        'new_src': 0xC59D,  # must match the POKE 23550 patched into line 900
    },
}
MAP_EXTRA_GLYPHS = {}
