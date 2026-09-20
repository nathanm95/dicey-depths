#!/usr/bin/env python3
"""Play thousands of runs and report how often they are won.

This parses the REAL item, enemy and class tables out of the C source, so the
numbers it tunes against cannot drift from the numbers the game ships. It does
re-implement the combat rules rather than executing combat.c, which is tied to
the Pebble APIs - so treat this as a tuning instrument, not proof. Anything it
flags should be confirmed by playing.

Simplifications, stated honestly:
  - Blind is ignored: hiding values from a policy that already knows them
    changes nothing here, though it matters a great deal to a person.
  - Shops buy the best affordable card; the anvil upgrades the best card.
  - The player fights the two cheapest-looking enemies on a floor, which is
    what a careful player does, so these are optimistic-ish win rates.
"""
import re, os, random, sys, collections

ROOT = os.path.join(os.path.dirname(__file__), "..", "src", "c")
def src(f): return open(os.path.join(ROOT, f)).read()

# ---------------------------------------------------------------- parsing
COND = {"COND_ANY":0,"COND_ODD":1,"COND_EVEN":2,"COND_EXACT":3,"COND_MIN":4,"COND_MAX":5}
EFF  = {"EFF_NONE":0,"EFF_DAMAGE":1,"EFF_SHIELD":2,"EFF_HEAL":3,
        "EFF_INFLICT":4,"EFF_SELF":5,"EFF_DRAIN":6,"EFF_REROLL":7}
ST   = {"ST_BURN":0,"ST_FROZEN":1,"ST_SHOCK":2,"ST_WEAKEN":3,
        "ST_BLIND":4,"ST_POISON":5,"ST_FURY":6,"ST_SILENCE":7}

def cond(tok):
    tok = tok.strip()
    m = re.match(r"C_(ANY|ODD|EVEN)$", tok)
    if m: return (COND["COND_"+m.group(1)], 0)
    m = re.match(r"C_(EXACT|MIN|MAX)\((\d+)\)$", tok)
    if m: return (COND["COND_"+m.group(1)], int(m.group(2)))
    m = re.match(r"\{COND_(\w+),\s*(\d+)\}$", tok)
    if m: return (COND["COND_"+m.group(1)], int(m.group(2)))
    return (0, 0)

def parse_items():
    body = src("items.c").split("static const ItemDef s_items[] = {",1)[1].split("\n};",1)[0]
    out = []
    for line in body.split("\n"):
        if '{"' not in line: continue
        name = re.search(r'\{"([^"]+)"', line).group(1)
        rest = line.split('"',2)[2]
        slots = int(re.match(r'\s*,\s*(\d+)', rest).group(1))
        conds = re.search(r"\{(C_[A-Z]+(?:\(\d+\))?(?:\s*,\s*C_[A-Z]+(?:\(\d+\))?)?)\}", rest).group(1)
        conds = [cond(c) for c in re.split(r",(?![^()]*\))", conds)]
        while len(conds) < 2: conds.append((0,0))
        effs_txt = re.search(r"\{\{(.+?)\}\s*,\s*(E_NONE|\{.+?\})\}", rest).groups()
        def eff(t):
            if t.strip() == "E_NONE": return (0,0,0,0)
            p = [x.strip() for x in t.strip().strip("{}").split(",")]
            return (EFF[p[0]], int(p[1]), int(p[2]), ST.get(p[3], 0))
        e0 = eff("{"+effs_txt[0]+"}")
        e1 = eff(effs_txt[1])
        tail = rest.split("}",)[-1]
        nums = re.findall(r",\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,", rest)
        uses, tier, price = (int(nums[-1][0]), int(nums[-1][1]), int(nums[-1][2])) if nums else (0,1,0)
        btok = rest.rstrip().rstrip("},").split(",")[-1].strip()
        bm = re.search(r"(C_\w+(?:\(\d+\))?|\{COND_\w+,\s*\d+\})\s*\}?,?\s*$", rest.strip())
        bonus = cond(bm.group(1)) if bm else (0,0)
        out.append(dict(name=name, slots=slots, cond=conds, eff=[e0,e1],
                        uses=uses, tier=tier, price=price, bonus=bonus))
    return out

def parse_enemies():
    body = src("enemies.c").split("static const EnemyDef s_enemies[] = {",1)[1].split("\n};",1)[0]
    out=[]
    for line in body.split("\n"):
        if '{"' not in line: continue
        name = re.search(r'\{"([^"]+)"', line).group(1)
        rest = line.split('"',2)[2]
        eq = re.search(r"\{([0-9,\s]+)\}", rest).group(1)
        equip = [int(x) for x in eq.split(",") if x.strip()]
        nums = re.findall(r"-?\d+", rest.split("{")[0])
        hp, dice = int(nums[0]), int(nums[1])
        after = rest.split("}",1)[1]
        a = re.findall(r"\d+", after)
        gold, dmin, dmax = int(a[1]), int(a[2]), int(a[3])
        out.append(dict(name=name, hp=hp, dice=dice, equip=equip, gold=gold,
                        dmin=dmin, dmax=dmax, boss="true}" in line.replace(" ","")))
    return out

def parse_loot():
    t = src("items.c"); out={}
    for arr in ("s_early","s_mid","s_late"):
        m=re.search(arr+r"\[\]\s*=\s*\{([^}]*)\}", t, re.S)
        out[arr]=[int(x) for x in re.findall(r"\d+", m.group(1))]
    return out

ITEMS, ENEMIES, LOOT = parse_items(), parse_enemies(), parse_loot()
BOSS_FIRST = int(re.search(r"#define BOSS_FIRST (\d+)", src("enemies.c")).group(1))
ITEM_REROLL = int(re.search(r"#define ITEM_REROLL\s+(\d+)", src("game.h")).group(1))
ELITE_PCT = 130
SUDDEN_PCT = 75
SUDDEN_DIE = 1
ELIM_FLOOR = {'Easy':5,'Normal':4,'Hard':3}
REROLL_BONUS = 1
DRAKE_HP = 50
DIFF = {"Normal": dict(ehp=100, inc=100, hhp=100),
        "Easy": dict(ehp=55, inc=45, hhp=160),
        "Hard": dict(ehp=125, inc=125, hhp=90)}

def accepts(c, pips):
    k,a = c
    return [True, pips%2==1, pips%2==0, pips==a, pips>=a, pips<=a][k]

def widens(idx):
    """The anvil widens a window rather than padding a number."""
    k = ITEMS[idx]["cond"][0][0]
    return k in (COND["COND_MAX"], COND["COND_MIN"])

def cond_up(idx, slot, up):
    k,a = ITEMS[idx]["cond"][slot]
    if not up or slot != 0: return (k,a)
    if k == COND["COND_MAX"] and a < 6: return (k, a+1)
    if k == COND["COND_MIN"] and a > 1: return (k, a-1)
    return (k,a)

# ---------------------------------------------------------------- combat
class Side:
    def __init__(s, hp, cards, dice):
        s.hp=s.max=hp; s.shield=0; s.cards=cards; s.dice_n=dice
        s.st=collections.defaultdict(int); s.charge=0; s.limit=False

def mkcards(ids, upg=()):
    return [dict(id=i, up=(n in upg), spent=False, uses=ITEMS[i]["uses"],
                 disabled=False, weak=False) for n,i in enumerate(ids)]

def fire(card, value, me, foe, d, inc_pct):
    it = ITEMS[card["id"]]
    raw = value
    if card["up"] and not widens(card["id"]): value += 1
    if card["weak"]: value = max(1, value//2); card["weak"]=False
    times = 2 if me.st[ST["ST_FURY"]]>0 else 1
    if times==2: me.st[ST["ST_FURY"]]-=1
    for _ in range(times):
        for n,(k,mult,add,stat) in enumerate(it["eff"]):
            if k==0: continue
            if n==1 and not accepts(it["bonus"], raw): continue
            v = value*mult+add
            if k==EFF["EFF_DAMAGE"]: hit(foe, v, False, inc_pct)
            elif k==EFF["EFF_SHIELD"]: me.shield += v
            elif k==EFF["EFF_HEAL"]:
                if v<0: hit(me, -v, True, inc_pct)
                else: me.hp=min(me.max, me.hp+v)
            elif k==EFF["EFF_DRAIN"]:
                hit(foe, v, False, inc_pct); me.hp=min(me.max, me.hp+v)
            elif k==EFF["EFF_INFLICT"]: foe.st[stat]+=1
            elif k==EFF["EFF_SELF"]: me.st[stat]+=1
    if it["uses"]: card["uses"]-=1
    card["spent"]=True

def hit(t, amount, pierce, inc_pct):
    if amount<=0: return
    if t.is_hero: amount = max(1, amount*inc_pct//100)
    if not pierce and t.shield>0:
        soak=min(t.shield,amount); t.shield-=soak; amount-=soak
    if amount<=0: return
    t.hp-=amount
    if t.is_hero:
        t.charge+=amount
        if t.charge>=t.max: t.limit=True; t.charge=t.max

def score(card, pips, me):
    it=ITEMS[card["id"]]; s=0
    for n,(k,mult,add,stat) in enumerate(it["eff"]):
        if k==0: continue
        if n==1 and not accepts(it["bonus"], pips): continue
        v=pips*mult+add
        if k==EFF["EFF_DAMAGE"]: s+=v
        elif k==EFF["EFF_DRAIN"]: s+=v*3//2
        elif k==EFF["EFF_SHIELD"]: s+=v*3//4
        elif k==EFF["EFF_HEAL"]: s+= v*2 if me.hp<me.max//2 else 1
        elif k in (EFF["EFF_INFLICT"],EFF["EFF_SELF"]): s+=4
        elif k==EFF["EFF_REROLL"]: s+=1
    return s

def take_turn(me, foe, robot, inc_pct, rule=0):
    me.shield=0
    for c in me.cards:
        c["spent"]=False; c["disabled"]=False; c["weak"]=False
        if ITEMS[c["id"]]["eff"][0][0]==EFF["EFF_REROLL"]:
            c["uses"]=max(1, me.dice_n-1+REROLL_BONUS)
    # statuses land on the roll
    if robot:
        dice=[]
        for _ in range(me.dice_n):
            cpu=0
            while True:
                cpu+=random.randint(1,4)
                if cpu>6: cpu=0; break
                if cpu>=4: break
            if cpu: dice.append(cpu)
    else:
        dice=[random.randint(1,6) for _ in range(me.dice_n)]
    if me.st[ST["ST_FROZEN"]]>0 and dice:
        if rule == 2: dice=[max(1,d-1) for d in dice]
        else: dice[dice.index(max(dice))]=1
        me.st[ST["ST_FROZEN"]]-=1
    burning=0
    if me.st[ST["ST_BURN"]]>0: burning=1; me.st[ST["ST_BURN"]]-=1
    if me.st[ST["ST_SHOCK"]]>0 and me.cards:
        random.choice(me.cards)["disabled"]=True; me.st[ST["ST_SHOCK"]]-=1
    if me.st[ST["ST_WEAKEN"]]>0 and me.cards:
        random.choice(me.cards)["weak"]=True; me.st[ST["ST_WEAKEN"]]-=1
    if me.st[ST["ST_POISON"]]>0:
        hit(me,3,True,inc_pct); me.st[ST["ST_POISON"]]-=1
    if me.hp<=0: return
    if burning and dice:
        if me.hp>2: hit(me,2,True,inc_pct)     # pay to put it out
        else: dice.pop()
    # Curse of Greed bills you a status at the start of every single turn.
    if rule == 1 and me.is_hero:
        me.st[random.randrange(0, ST["ST_FURY"])] += 1
    if me.is_hero and me.limit and not robot:
        me.st[ST["ST_FURY"]]+=1; me.limit=False; me.charge=0

    used=[False]*len(dice)
    while True:
        best=None   # (score, [die indices], card)
        for c in me.cards:
            it=ITEMS[c["id"]]
            if c["spent"] or c["disabled"]: continue
            if it["uses"] and c["uses"]<=0: continue
            if it["eff"][0][0]==EFF["EFF_REROLL"]: continue      # handled below
            if it["slots"]==1:
                for di,p in enumerate(dice):
                    if used[di] or not accepts(cond_up(c["id"],0,c["up"]), p): continue
                    sc=score(c,p,me)
                    if best is None or sc>best[0]: best=(sc,[di],c)
            else:
                # Two-slot cards are among the strongest in the game; a policy
                # that ignores them badly understates what a player can do.
                for a in range(len(dice)):
                    if used[a] or not accepts(cond_up(c["id"],0,c["up"]), dice[a]): continue
                    for b in range(len(dice)):
                        if b==a or used[b] or not accepts(it["cond"][1], dice[b]): continue
                        sc=score(c, dice[a]+dice[b], me)
                        if best is None or sc>best[0]: best=(sc,[a,b],c)
        # Before committing, upgrade a dud: a 1 or 2 with a reroll spare and
        # another die to feed it is nearly always worth improving.
        if best and best[0] <= 2:
            rc=None
            for c2 in me.cards:
                if (ITEMS[c2["id"]]["eff"][0][0]==EFF["EFF_REROLL"]
                        and c2["uses"]>0 and not c2["disabled"]):
                    rc=c2; break
            spare=[i for i,_ in enumerate(dice) if not used[i]]
            if rc and len(spare)>=2:
                spare.sort(key=lambda i: dice[i])
                used[spare[0]]=True
                dice[spare[1]]=random.randint(1,6)
                rc["uses"]-=1
                continue
        if best:
            _,dis,c = best
            total=sum(dice[i] for i in dis)
            for i in dis: used[i]=True
            fire(c, total, me, foe, dice, inc_pct)
            if foe.hp<=0: return
            continue

        # Nothing worth placing. The Warrior's whole feature is turning a poor
        # die into a better one, so spend a reroll on the worst spare before
        # giving the turn up.
        rc=None
        for c in me.cards:
            if ITEMS[c["id"]]["eff"][0][0]==EFF["EFF_REROLL"] and c["uses"]>0 and not c["disabled"]:
                rc=c; break
        spare=[i for i,_ in enumerate(dice) if not used[i]]
        if rc and len(spare)>=2:
            spare.sort(key=lambda i: dice[i])
            feed=spare[0]; target=spare[1] if dice[spare[1]]<=2 else spare[0]
            if target==feed and len(spare)>1: target=spare[1]
            used[feed]=True
            dice[target]=random.randint(1,6)
            rc["uses"]-=1
            continue
        break

def fight(hero_cards, hero_hp, hero_max, dice, enemy, diff, robot, elite, rule=0):
    inc = DIFF[diff]["inc"]
    hero=Side(hero_max,hero_cards,dice); hero.hp=hero_hp; hero.is_hero=True
    ehp = enemy["hp"]*DIFF[diff]["ehp"]//100
    if elite: ehp = ehp*ELITE_PCT//100
    foe=Side(ehp, mkcards(enemy["equip"]), enemy["dice"]+(1 if elite else 0))
    foe.is_hero=False
    # A generous cap: a real fight that has not resolved in 120 turns is a
    # genuine stalemate, not a slow win. Counting a long fight as a LOSS was
    # quietly inventing defeats - on Easy it reported 15% losses where the
    # simulated player had died exactly zero times.
    for _ in range(120):
        take_turn(hero, foe, robot, inc, rule)
        if foe.hp<=0: return True, hero.hp, hero.charge, hero.limit
        take_turn(foe, hero, False, inc, 0)
        if hero.hp<=0: return False, 0, 0, False
    return False, hero.hp, hero.charge, hero.limit   # true stalemate

# ---------------------------------------------------------------- run
# Probability a random die satisfies a slot, used to value fussy cards honestly.
def _p_accept(c):
    k,a = c
    return [1.0, 0.5, 0.5, 1/6.0, (7-a)/6.0, a/6.0][k]

def card_value(idx):
    """Rough worth of a card at an average roll, discounted by how often its
    slot will actually accept a die. Used to choose what to carry."""
    it = ITEMS[idx]
    ev = 0.0
    for n,(k,mult,add,stat) in enumerate(it["eff"]):
        if k==0: continue
        if n==1: ev += 2.0 * _p_accept(it["bonus"]); continue
        v = 3.5*mult + add
        if   k==EFF["EFF_DAMAGE"]: ev += v
        elif k==EFF["EFF_DRAIN"]:  ev += v*1.5
        elif k==EFF["EFF_SHIELD"]: ev += v*0.75
        elif k==EFF["EFF_HEAL"]:   ev += v*0.5
        elif k==EFF["EFF_REROLL"]: ev += 3.0
        else: ev += 3.0
    p = _p_accept(it["cond"][0])
    if it["slots"] > 1: p *= _p_accept(it["cond"][1]) * 0.6   # costs two dice
    if it["uses"]: ev *= 0.7
    return ev * p

def best_loadout(pool, slots):
    """What a player would actually carry. The backpack exists precisely so a
    better card can be swapped in; a policy that never swaps understates the
    player badly - it was the reason a four-card starting kit scored 0%."""
    ranked = sorted(set(pool), key=card_value, reverse=True)
    return ranked[:slots]

def loot_for(depth):
    t = LOOT["s_early"] if depth<=1 else (LOOT["s_mid"] if depth==2 else LOOT["s_late"])
    return random.choice(t)

def play_run(klass, level, diff):
    """level is 1..5 and maps to campaign_level 0..4 in the C source."""
    rule = level - 1                      # 0 plain, 1 greed, 2 parallel, 3 elim, 4 sudden
    robot = (klass=="Robot")
    base = 24
    max_hp = base*DIFF[diff]["hhp"]//100
    if rule == 4: max_hp = max_hp//2                       # Sudden Death
    hp = max_hp
    sig = ITEM_REROLL if not robot else 7
    ids = [0, 1, sig, 5]
    upg = set()
    if rule == 1:                                          # Curse of Greed
        ids = [2, 8, sig, 1]; upg = {0, 1}
    slots = 6 if rule >= 3 else 4                          # Elimination onward
    gold = 0
    pool = list(ids)                                       # everything owned
    for floor in (1,2,3):
        dice = 2 if floor==1 else (3 if floor==2 else 4)
        if rule == 4: dice += SUDDEN_DIE
        if floor==3:
            hp = min(max_hp, hp+8)
            boss = dict(ENEMIES[BOSS_FIRST+rule])
            if DRAKE_HP and boss['name']=='The Drake': boss['hp']=DRAKE_HP
            won,hp,_,_ = fight(mkcards(ids,upg), hp, max_hp, dice, boss,
                               diff, robot, False, rule)
            return won
        foes=[e for e in ENEMIES if not e["boss"] and e["dmin"]<=floor<=e["dmax"]]
        picks=sorted(random.sample(foes,min(3,len(foes))), key=lambda e:e["hp"])[:2]
        for i,e in enumerate(picks):
            elite = (floor>=2 and i==1 and random.random()<0.6)
            won,hp,_,_ = fight(mkcards(ids,upg), hp, max_hp, dice, e,
                               diff, robot, elite, rule)
            if not won: return False
            gold += e["gold"]*(2 if elite else 1)*(2 if rule==4 else 1)
            if rule == 3 and len(pool) > ELIM_FLOOR[diff]:   # Elimination Round
                drop = random.choice(ids) if ids else pool[0]
                if drop in pool: pool.remove(drop)
                ids = best_loadout(pool, slots)
        hp = min(max_hp, hp+8)
        pool.append(loot_for(floor))                       # chest
        for _ in range(3):                                 # a shop stocks three
            if gold < 14: break
            pool.append(loot_for(floor)); gold -= 14
        ids = best_loadout(pool, slots)
        if floor>=2 and ids: upg.add(0)                    # anvil
        max_hp += 4; hp = max_hp
    return False

def play_endless(klass, diff, cap=40):
    """How deep does an endless run get? Returns floors cleared."""
    robot = (klass=="Robot")
    max_hp = 24*DIFF[diff]["hhp"]//100
    hp = max_hp
    ids = [0,1,ITEM_REROLL if not robot else 7, 5]
    pool = list(ids)
    upg=set(); gold=0
    for floor in range(1, cap+1):
        dice = min(4, 1+floor) if floor<4 else 4
        slots = 6 if floor>=5 else 4
        foes=[e for e in ENEMIES if not e["boss"] and e["dmin"]<=min(floor,6)<=e["dmax"]]
        if not foes: break
        picks=sorted(random.sample(foes,min(3,len(foes))), key=lambda e:e["hp"])[:2]
        for i,e in enumerate(picks):
            scaled = dict(e); scaled["hp"] = e["hp"]*(100+12*floor)//100
            scaled["dice"] = e["dice"] + floor//4
            elite = (floor>=2 and i==1 and random.random()<0.6)
            won,hp,_,_ = fight(mkcards(ids,upg), hp, max_hp, dice, scaled,
                               diff, robot, elite, 0)
            if not won: return floor-1
            gold += e["gold"]
        hp = min(max_hp, hp+8)
        pool.append(loot_for(min(floor,3)))
        for _ in range(3):
            if gold < 14: break
            pool.append(loot_for(min(floor,3))); gold -= 14
        ids = best_loadout(pool, slots)
        if ids: upg.add(0)
        max_hp += 4; hp = max_hp
    return cap

if len(sys.argv)>1 and sys.argv[1]=="endless":
    n=int(sys.argv[2]) if len(sys.argv)>2 else 400
    print("endless: floors cleared before dying")
    print("%-10s %-8s %-8s %-8s %-8s" % ("class/diff","median","mean","best","p90"))
    print("-"*46)
    for klass in ("Warrior","Robot"):
        for diff in ("Easy","Normal","Hard"):
            r=sorted(play_endless(klass,diff) for _ in range(n))
            med=r[len(r)//2]; mean=sum(r)/len(r); p90=r[int(len(r)*0.9)]
            print("%-10s %-8s %-8.1f %-8s %-8s" % (klass[:4]+"/"+diff[:4], med, mean, r[-1], p90))
    sys.exit(0)

N = int(sys.argv[1]) if len(sys.argv)>1 else 2000
names = ["Opening Round","Curse of Greed","Parallel Universe","Elimination","Sudden Death"]
DIFFS = ("Easy","Normal","Hard")
for klass in ("Warrior","Robot"):
    print("%-22s %s" % (klass, "  ".join("%-7s"%d for d in DIFFS)))
    print("-"*46)
    for lvl in range(1,6):
        row=[]
        for diff in DIFFS:
            wins=sum(play_run(klass,lvl,diff) for _ in range(N))
            row.append("%5.1f%%" % (100.0*wins/N))
        print("%-22s %s" % ("%d. %s"%(lvl,names[lvl-1]), "  ".join("%-7s"%r for r in row)))
    print()
