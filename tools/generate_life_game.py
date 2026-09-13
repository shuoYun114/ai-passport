# -*- coding: utf-8 -*-
"""
生成《人生重开模拟器》游戏数据头文件，并自动提取所有汉字字符，使用 lv_font_conv 生成字库。
"""
import os
import subprocess
import json

# 1. 24个天赋定义
# quality: 0=普通(白), 1=稀有(蓝), 2=史诗(紫), 3=神话(金)
TALENTS = [
    # 金色(神级)
    {"id": 0, "name": "神秘小盒子", "desc": "100岁开启修仙大门", "q": 3, "chr": 0, "int": 0, "str": 0, "mny": 0, "joy": 0},
    {"id": 1, "name": "赛博意识流", "desc": "家境+3智力+3解锁永生", "q": 3, "chr": 0, "int": 3, "str": 0, "mny": 3, "joy": 0},
    {"id": 2, "name": "天选之子", "desc": "全属性+2好运常伴", "q": 3, "chr": 2, "int": 2, "str": 2, "mny": 2, "joy": 2},
    {"id": 3, "name": "半神血脉", "desc": "体质+5百病不侵", "q": 3, "chr": 0, "int": 0, "str": 5, "mny": 0, "joy": 0},
    # 紫色(史诗)
    {"id": 4, "name": "先天灵根", "desc": "体质+2智力+2易感灵气", "q": 2, "chr": 0, "int": 2, "str": 2, "mny": 0, "joy": 0},
    {"id": 5, "name": "矿石大亨", "desc": "家境初始+5", "q": 2, "chr": 0, "int": 0, "str": 0, "mny": 5, "joy": 0},
    {"id": 6, "name": "盛世美颜", "desc": "颜值初始+5", "q": 2, "chr": 5, "int": 0, "str": 0, "mny": 0, "joy": 0},
    {"id": 7, "name": "超凡大脑", "desc": "智力初始+5", "q": 2, "chr": 0, "int": 5, "str": 0, "mny": 0, "joy": 0},
    {"id": 8, "name": "金刚不坏", "desc": "体质+4免疫外伤", "q": 2, "chr": 0, "int": 0, "str": 4, "mny": 0, "joy": 0},
    {"id": 9, "name": "黑客宗师", "desc": "智力+3家境+2网络大牛", "q": 2, "chr": 0, "int": 3, "str": 0, "mny": 2, "joy": 0},
    # 蓝色(稀有)
    {"id": 10, "name": "运动健将", "desc": "体质初始+3", "q": 1, "chr": 0, "int": 0, "str": 3, "mny": 0, "joy": 0},
    {"id": 11, "name": "学霸附体", "desc": "智力初始+3", "q": 1, "chr": 0, "int": 3, "str": 0, "mny": 0, "joy": 0},
    {"id": 12, "name": "社交达人", "desc": "颜值+2快乐+2", "q": 1, "chr": 2, "int": 0, "str": 0, "mny": 0, "joy": 2},
    {"id": 13, "name": "中产家庭", "desc": "家境初始+3", "q": 1, "chr": 0, "int": 0, "str": 0, "mny": 3, "joy": 0},
    {"id": 14, "name": "锦鲤附身", "desc": "快乐初始+3", "q": 1, "chr": 0, "int": 0, "str": 0, "mny": 0, "joy": 3},
    {"id": 15, "name": "极客少年", "desc": "智力+2家境+1", "q": 1, "chr": 0, "int": 2, "str": 0, "mny": 1, "joy": 0},
    # 白色(普通)
    {"id": 16, "name": "早睡早起", "desc": "体质+1快乐+1", "q": 0, "chr": 0, "int": 0, "str": 1, "mny": 0, "joy": 1},
    {"id": 17, "name": "乐天派", "desc": "快乐初始+2", "q": 0, "chr": 0, "int": 0, "str": 0, "mny": 0, "joy": 2},
    {"id": 18, "name": "好奇宝宝", "desc": "智力初始+1", "q": 0, "chr": 0, "int": 1, "str": 0, "mny": 0, "joy": 0},
    {"id": 19, "name": "和蔼可亲", "desc": "颜值初始+1", "q": 0, "chr": 1, "int": 0, "str": 0, "mny": 0, "joy": 0},
    {"id": 20, "name": "勤俭节约", "desc": "家境初始+1", "q": 0, "chr": 0, "int": 0, "str": 0, "mny": 1, "joy": 0},
    {"id": 21, "name": "吸猫体质", "desc": "快乐+2容易招猫喜欢", "q": 0, "chr": 0, "int": 0, "str": 0, "mny": 0, "joy": 2},
    {"id": 22, "name": "美食达人", "desc": "体质+1家境-1好吃零食", "q": 0, "chr": 0, "int": 0, "str": 1, "mny": -1, "joy": 1},
    {"id": 23, "name": "掌机狂人", "desc": "快乐+2智力+1体质-1", "q": 0, "chr": 0, "int": 1, "str": -1, "mny": 0, "joy": 2},
]

# 2. 人生关键抉择 (Choices)
CHOICES = [
    {
        "id": 1,
        "age": 18,
        "title": "高考前夕重要抉择",
        "opt_a": "挑灯夜战考名校",
        "opt_b": "瞒着家里打电竞",
        "res_a": "你考入顶尖名校计算机系！",
        "res_b": "你成为电竞战队黑马新星！",
        "da": {"int": 3, "mny": 2, "joy": -1, "str": -1},
        "db": {"joy": 4, "mny": 3, "str": -2, "int": 0}
    },
    {
        "id": 2,
        "age": 23,
        "title": "大学毕业十字路口",
        "opt_a": "进大厂996硬核奋斗",
        "opt_b": "考公上岸清闲摸鱼",
        "res_a": "你拿到顶薪年薪百万！",
        "res_b": "你过上朝九晚五惬意生活！",
        "da": {"mny": 5, "str": -3, "joy": -1, "int": 2},
        "db": {"joy": 4, "str": 2, "mny": 1, "int": 0}
    },
    {
        "id": 3,
        "age": 30,
        "title": "神秘人递来黑色药丸",
        "opt_a": "一口吞下激发潜能",
        "opt_b": "果断上交领五百元",
        "res_a": "你感觉大脑超频，灵光乍现！",
        "res_b": "你被评为年度优秀守法热心市民！",
        "da": {"int": 6, "str": 2, "joy": 2, "mny": 0},
        "db": {"mny": 1, "joy": 3, "int": 0, "str": 0}
    },
    {
        "id": 4,
        "age": 80,
        "title": "脑机接口数字飞升邀请",
        "opt_a": "签署协议意识上传",
        "opt_b": "顺应天命安享晚年",
        "res_a": "你的意识跨入量子矩阵，开启数字永生！",
        "res_b": "你坐在摇椅上品茶，看子孙满堂。",
        "da": {"cyber": 1, "int": 10, "joy": 5},
        "db": {"joy": 5, "str": 1}
    },
    {
        "id": 5,
        "age": 120,
        "title": "筑基天劫降临九霄",
        "opt_a": "引天雷入体强行筑基",
        "opt_b": "服九转灵丹稳健突破",
        "res_a": "紫霄神雷淬体！你成就天道天品金刚筑基！",
        "res_b": "药力温润，你水到渠成突破人道筑基！",
        "da": {"str": 15, "int": 10, "joy": 5},
        "db": {"str": 8, "int": 5, "joy": 3}
    }
]

# 3. 年龄事件库 (包含幼年、童年、青年、中年、晚年、修仙、赛博与突发死因)
EVENTS = [
    # 0岁~5岁 幼年期
    {"min_a": 0, "max_a": 0, "req_attr": "", "val": 0, "text": "你出生了，是个健康的宝宝，父母喜极而泣。", "d": {"joy": 1}},
    {"min_a": 0, "max_a": 0, "req_attr": "mny", "val": 8, "text": "你含着金汤匙降生在独栋大别墅里。", "d": {"joy": 2, "mny": 1}},
    {"min_a": 1, "max_a": 1, "req_attr": "", "val": 0, "text": "你学会了爬行，第一件事就是咬断了爸爸的网线。", "d": {"int": 1}},
    {"min_a": 1, "max_a": 1, "req_attr": "str", "val": 7, "text": "你一岁就能单手把家里的猫咪抱起来跑。", "d": {"str": 1}},
    {"min_a": 2, "max_a": 2, "req_attr": "", "val": 0, "text": "抓周宴上你越过所有金银玩具，抓起了一块开发板。", "d": {"int": 1, "joy": 1}},
    {"min_a": 2, "max_a": 2, "req_attr": "chr", "val": 7, "text": "邻居阿姨路过看了你一眼，夸你像洋娃娃一样可爱。", "d": {"chr": 1, "joy": 1}},
    {"min_a": 3, "max_a": 3, "req_attr": "", "val": 0, "text": "你上了幼儿园，因为抢积木被老师罚站了十分钟。", "d": {"joy": -1}},
    {"min_a": 3, "max_a": 3, "req_attr": "int", "val": 7, "text": "你在幼儿园轻松用积木拼出了一艘星际战舰模型。", "d": {"int": 2}},
    {"min_a": 4, "max_a": 4, "req_attr": "", "val": 0, "text": "你试图跟村头的大白鹅决斗，被追着跑了三条街。", "d": {"str": 1, "joy": -1}},
    {"min_a": 5, "max_a": 5, "req_attr": "", "val": 0, "text": "你学会了用手机看动画片，一整天都很安静。", "d": {"joy": 1}},
    {"min_a": 5, "max_a": 5, "req_attr": "int", "val": 8, "text": "你已经能心算出三位数加减法，家人惊呼神童！", "d": {"int": 2, "joy": 2}},

    # 6岁~12岁 小学期
    {"min_a": 6, "max_a": 6, "req_attr": "", "val": 0, "text": "你背上书包上了小学，同桌分给你半包辣条。", "d": {"joy": 1}},
    {"min_a": 7, "max_a": 7, "req_attr": "str", "val": 8, "text": "校运会五十米冲刺你打破了全校低年级纪录！", "d": {"str": 2, "joy": 2}},
    {"min_a": 7, "max_a": 7, "req_attr": "", "val": 0, "text": "你因为上课偷偷看漫画书，被班主任叫了家长。", "d": {"joy": -1}},
    {"min_a": 8, "max_a": 8, "req_attr": "mny", "val": 8, "text": "爸爸送给你一台最新款掌机作为生日礼物！", "d": {"joy": 3, "int": 1}},
    {"min_a": 8, "max_a": 8, "req_attr": "", "val": 0, "text": "你和几个好朋友在小树林里探险，捡到了奇特矿石。", "d": {"str": 1}},
    {"min_a": 9, "max_a": 9, "req_attr": "int", "val": 8, "text": "你参加全国青少年科技小发明比赛荣获特等奖！", "d": {"int": 3, "mny": 1}},
    {"min_a": 10, "max_a": 10, "req_attr": "", "val": 0, "text": "你第一次接触电脑编程，写出了一个贪吃蛇游戏！", "d": {"int": 2, "joy": 2}},
    {"min_a": 11, "max_a": 11, "req_attr": "chr", "val": 8, "text": "你成了少先队大队长，走在操场上备受瞩目。", "d": {"chr": 1, "joy": 2}},
    {"min_a": 12, "max_a": 12, "req_attr": "", "val": 0, "text": "小学毕业了，你在同学录上写下要成为伟大人物。", "d": {"joy": 1}},

    # 13岁~18岁 中学期
    {"min_a": 13, "max_a": 13, "req_attr": "", "val": 0, "text": "进入初中，你开始快速拔高，声线也变得沉稳。", "d": {"str": 1}},
    {"min_a": 14, "max_a": 14, "req_attr": "int", "val": 9, "text": "你自学了微积分和量子力学基础，老师自叹不如。", "d": {"int": 3}},
    {"min_a": 14, "max_a": 14, "req_attr": "chr", "val": 8, "text": "你的课桌抽屉里塞满了粉红色的表白信纸。", "d": {"chr": 1, "joy": 2}},
    {"min_a": 15, "max_a": 15, "req_attr": "", "val": 0, "text": "中考顺利考入重点高中，父母在饭店摆了三桌庆祝。", "d": {"joy": 2, "mny": -1}},
    {"min_a": 16, "max_a": 16, "req_attr": "", "val": 0, "text": "高中的学业压力繁重，你每天喝两罐红牛提神。", "d": {"str": -1, "int": 1}},
    {"min_a": 17, "max_a": 17, "req_attr": "str", "val": 9, "text": "你在校篮球联赛中压哨绝杀，全场女生为你尖叫！", "d": {"chr": 2, "joy": 3}},

    # 19岁~25岁 青年期
    {"min_a": 19, "max_a": 19, "req_attr": "", "val": 0, "text": "大学生活丰富多彩，你参加了机器人社团担任队长。", "d": {"int": 2, "joy": 1}},
    {"min_a": 20, "max_a": 20, "req_attr": "int", "val": 10, "text": "你在开源社区发布的AI模型获得了全球开发者的赞誉！", "d": {"int": 3, "mny": 2}},
    {"min_a": 20, "max_a": 20, "req_attr": "chr", "val": 9, "text": "你与心仪已久的校花开启了一段甜蜜的初恋之旅。", "d": {"joy": 4, "mny": -1}},
    {"min_a": 21, "max_a": 21, "req_attr": "mny", "val": 10, "text": "你抓住风口投资虚拟资产，大赚了一笔首付钱！", "d": {"mny": 4, "joy": 2}},
    {"min_a": 22, "max_a": 22, "req_attr": "", "val": 0, "text": "大学毕业典礼上，你把学士帽高高抛向空中。", "d": {"joy": 1}},
    {"min_a": 24, "max_a": 24, "req_attr": "", "val": 0, "text": "步入职场的你渐渐懂得了人情世故与成年人的坚韧。", "d": {"int": 1}},
    {"min_a": 25, "max_a": 25, "req_attr": "str", "val": 10, "text": "你报名参加了全马马拉松，竟然跑进了前一百名！", "d": {"str": 2, "joy": 2}},

    # 26岁~45岁 壮年期
    {"min_a": 26, "max_a": 26, "req_attr": "", "val": 0, "text": "你与深爱的人步入婚姻殿堂，许下一生的誓言。", "d": {"joy": 4, "mny": -2}},
    {"min_a": 28, "max_a": 28, "req_attr": "", "val": 0, "text": "伴随着一声清脆的啼哭，你的宝贝降生在人世间！", "d": {"joy": 5, "mny": -2}},
    {"min_a": 32, "max_a": 32, "req_attr": "mny", "val": 12, "text": "你带领团队创立的科技公司成功上市，敲钟纳斯达克！", "d": {"mny": 8, "joy": 4}},
    {"min_a": 35, "max_a": 35, "req_attr": "", "val": 0, "text": "人到中年，你开始在保温杯里放枸杞，更加注重养生。", "d": {"str": 1}},
    {"min_a": 38, "max_a": 38, "req_attr": "int", "val": 12, "text": "你出版的行业专业论著成为高等院校指定教材！", "d": {"int": 3, "mny": 2}},
    {"min_a": 42, "max_a": 42, "req_attr": "", "val": 0, "text": "你升任为集团高级合伙人，手握重大决策权。", "d": {"mny": 3, "int": 1}},
    {"min_a": 45, "max_a": 45, "req_attr": "str", "val": 11, "text": "你在户外攀登成功登顶乞力马扎罗雪山，豪气冲天！", "d": {"str": 2, "joy": 3}},

    # 46岁~70岁 熟年期
    {"min_a": 50, "max_a": 50, "req_attr": "", "val": 0, "text": "年过半百，知天命之年，心境愈发豁达从容。", "d": {"joy": 2}},
    {"min_a": 55, "max_a": 55, "req_attr": "", "val": 0, "text": "儿女成才各自成家，你放下了心中的千斤重担。", "d": {"joy": 3}},
    {"min_a": 60, "max_a": 60, "req_attr": "", "val": 0, "text": "你光荣退休，开始周游世界，饱览名山大川。", "d": {"joy": 3, "str": 1}},
    {"min_a": 65, "max_a": 65, "req_attr": "", "val": 0, "text": "可爱的孙子趴在你的膝盖上，听你讲当年的传奇故事。", "d": {"joy": 3}},
    {"min_a": 70, "max_a": 70, "req_attr": "str", "val": 10, "text": "你晨练打太极拳被拍成视频，在短视频平台狂揽百万赞！", "d": {"joy": 2, "str": 1}},

    # 71岁~99岁 晚年期
    {"min_a": 75, "max_a": 75, "req_attr": "", "val": 0, "text": "体检显示你的各项生理机能依然堪比中年人，医生称奇！", "d": {"str": 1, "joy": 1}},
    {"min_a": 85, "max_a": 85, "req_attr": "", "val": 0, "text": "四世同堂其乐融融，你成为家族中最受尊敬的长者。", "d": {"joy": 3}},
    {"min_a": 90, "max_a": 90, "req_attr": "", "val": 0, "text": "你九十大寿那天，当地媒体特地前来登门祝贺。", "d": {"joy": 2}},
    {"min_a": 95, "max_a": 95, "req_attr": "", "val": 0, "text": "满头银丝鹤发童颜，你静坐庭院看花开花落云卷云舒。", "d": {"joy": 2}},
    {"min_a": 99, "max_a": 99, "req_attr": "", "val": 0, "text": "百岁大关在即，你感到身体深处有一股神秘能量在苏醒……", "d": {"joy": 1}},

    # 100岁+ 修仙路线专属
    {"min_a": 100, "max_a": 100, "req_attr": "cult", "val": 1, "text": "脑海中小盒子咔哒解封！上古太玄经涌入识海，你开启修仙！", "d": {"str": 20, "int": 15, "joy": 10}},
    {"min_a": 105, "max_a": 105, "req_attr": "cult", "val": 1, "text": "天地灵气倒灌！你排出全身杂质，成功踏入【练气期一层】！", "d": {"str": 10, "int": 10}},
    {"min_a": 115, "max_a": 115, "req_attr": "cult", "val": 1, "text": "你吞吐朝阳紫气，容颜逆生长为青葱少年，寿元延至150岁！", "d": {"chr": 5, "str": 10}},
    {"min_a": 130, "max_a": 130, "req_attr": "cult", "val": 1, "text": "你深入南极冰川寻得千年玄冰，炼制出本命飞剑【青冥】！", "d": {"str": 15, "int": 10}},
    {"min_a": 160, "max_a": 160, "req_attr": "cult", "val": 1, "text": "修仙界妖兽暴动，你一剑光寒十九洲，斩灭九头妖王！", "d": {"str": 20, "int": 15, "joy": 5}},
    {"min_a": 200, "max_a": 200, "req_attr": "cult", "val": 1, "text": "九色祥云笼罩千里！你凝聚至高九转金丹，寿元达500岁！", "d": {"str": 30, "int": 20, "joy": 10}},
    {"min_a": 280, "max_a": 280, "req_attr": "cult", "val": 1, "text": "金丹碎裂元婴凝结！你神识笼罩四海，入【元婴真君】境！", "d": {"str": 40, "int": 30, "joy": 10}},
    {"min_a": 400, "max_a": 400, "req_attr": "cult", "val": 1, "text": "九天神雷混沌降世！你肉身硬抗九九八十一道天劫肉身成圣！", "d": {"str": 50, "int": 40, "joy": 10}},
    {"min_a": 500, "max_a": 500, "req_attr": "cult", "val": 1, "text": "天门洞开仙乐齐鸣！你白日飞升登临无上仙界，与天地同寿！", "d": {"joy": 99}},

    # 赛博分支事件 (81岁+)
    {"min_a": 81, "max_a": 81, "req_attr": "cyber", "val": 1, "text": "你的数字意识在超导服务器中苏醒，算力超越整座地球！", "d": {"int": 30, "joy": 5}},
    {"min_a": 88, "max_a": 88, "req_attr": "cyber", "val": 1, "text": "你重构了全人类网络安全底座，彻底清除了所有恶性病毒！", "d": {"int": 20, "joy": 5}},
    {"min_a": 100, "max_a": 100, "req_attr": "cyber", "val": 1, "text": "你攻克了室温超导与恒星戴森球矩阵，人类跨入星际文明！", "d": {"int": 30, "joy": 10}},
    {"min_a": 150, "max_a": 150, "req_attr": "cyber", "val": 1, "text": "你的意志与全宇宙量子纠缠，成为了永恒的赛博神明！", "d": {"int": 50, "joy": 99}},

    # 搞笑/突发死因与意外 (体质过低或偶发事件)
    {"min_a": 2, "max_a": 2, "req_attr": "str_low", "val": 2, "text": "你好奇地吞咽了一颗积木玩具不幸窒息离世。", "d": {"die": 1}},
    {"min_a": 8, "max_a": 8, "req_attr": "str_low", "val": 2, "text": "你为了抢救掉落池塘的小猫不幸失足溺水身亡。", "d": {"die": 1}},
    {"min_a": 28, "max_a": 35, "req_attr": "str_low", "val": 3, "text": "你连续加班七十二小时突发心梗抢救无效逝世。", "d": {"die": 1}},
    {"min_a": 40, "max_a": 50, "req_attr": "joy_low", "val": 1, "text": "由于长期心情抑郁忧思过度你终日郁郁寡欢而终。", "d": {"die": 1}},
]

# 4. 人生结局与称号评级
TITLES = [
    {"min_score": 180, "name": "无上至尊真仙", "eval": "突破虚空白日飞升与日月同辉！"},
    {"min_score": 140, "name": "数字赛博神明", "eval": "算力主宰宇宙意识化为永恒！"},
    {"min_score": 110, "name": "一代绝世传奇", "eval": "名垂青史流芳百世的大人物！"},
    {"min_score": 85,  "name": "商界百亿巨擘", "eval": "富甲一方儿孙满堂的巅峰人生！"},
    {"min_score": 60,  "name": "幸福圆满长者", "eval": "平淡是真安享天伦的福寿人生！"},
    {"min_score": 35,  "name": "平凡打工凡人", "eval": "酸甜苦辣尝遍虽普通但真实的人生！"},
    {"min_score": 0,   "name": "倒霉早夭顽童", "eval": "出师未捷身先死，下辈子再做欧皇！"},
]

def collect_all_chinese_chars():
    chars = set()
    for t in TALENTS:
        for ch in t["name"] + t["desc"]:
            if '\u4e00' <= ch <= '\u9fff' or ch in '，。！？、：；【】（）《》':
                chars.add(ch)
    for c in CHOICES:
        for ch in c["title"] + c["opt_a"] + c["opt_b"] + c["res_a"] + c["res_b"]:
            if '\u4e00' <= ch <= '\u9fff' or ch in '，。！？、：；【】（）《》':
                chars.add(ch)
    for e in EVENTS:
        for ch in e["text"]:
            if '\u4e00' <= ch <= '\u9fff' or ch in '，。！？、：；【】（）《》':
                chars.add(ch)
    for t in TITLES:
        for ch in t["name"] + t["eval"]:
            if '\u4e00' <= ch <= '\u9fff' or ch in '，。！？、：；【】（）《》':
                chars.add(ch)
    ui_words = "人生重开模拟器立即重开天赋抽选属性分配踏入轮回岁颜值智力体质家境快乐开始重开享年评分生平称号综合评价按键操作选择确认下一年长按退出选项修仙赛博历史记录当前剩余点数普通稀有史诗神话"
    for ch in ui_words:
        if '\u4e00' <= ch <= '\u9fff' or ch in '，。！？、：；【】（）《》':
            chars.add(ch)
    return chars

def update_fonts(new_chars):
    with open('available_chars.txt', 'r', encoding='utf-8') as f:
        existing = f.read()
    total_set = set(existing) | new_chars
    total_str = ''.join(sorted(list(total_set)))
    print(f"Total unique symbols to generate: {len(total_str)}")
    
    font_path = r"C:\Windows\Fonts\simhei.ttf"
    
    cmd16 = [
        "npx", "lv_font_conv",
        "--no-compress", "--no-prefilter", "--no-kerning",
        "--bpp", "4", "--size", "16",
        "--font", font_path,
        "-r", "0x20-0x7f",
        "--symbols", total_str,
        "--format", "lvgl",
        "--lv-include", "lvgl.h",
        "--lv-font-name", "buddy_font_zh_16",
        "-o", r"firmware\main\buddy_font_zh_16.c"
    ]
    print("Generating buddy_font_zh_16.c ...")
    subprocess.run(cmd16, check=True, shell=True)

    cmd14 = [
        "npx", "lv_font_conv",
        "--no-compress", "--no-prefilter", "--no-kerning",
        "--bpp", "4", "--size", "14",
        "--font", font_path,
        "-r", "0x20-0x7f",
        "--symbols", total_str,
        "--format", "lvgl",
        "--lv-include", "lvgl.h",
        "--lv-font-name", "buddy_font_zh_14",
        "-o", r"firmware\main\buddy_font_zh_14.c"
    ]
    print("Generating buddy_font_zh_14.c ...")
    subprocess.run(cmd14, check=True, shell=True)
    print("Fonts successfully updated!")

def generate_c_data_header():
    out = []
    out.append('#ifndef BUDDY_GAME_LIFE_DATA_H')
    out.append('#define BUDDY_GAME_LIFE_DATA_H')
    out.append('')
    out.append('#include <stdint.h>')
    out.append('#include <stdbool.h>')
    out.append('')
    
    out.append('typedef struct {')
    out.append('    uint8_t id;')
    out.append('    uint8_t quality; // 0:白 1:蓝 2:紫 3:金')
    out.append('    const char *name;')
    out.append('    const char *desc;')
    out.append('    int8_t chr;')
    out.append('    int8_t int_val;')
    out.append('    int8_t str;')
    out.append('    int8_t mny;')
    out.append('    int8_t joy;')
    out.append('} life_talent_def_t;')
    out.append('')
    
    out.append(f'#define LIFE_TALENT_COUNT {len(TALENTS)}')
    out.append('static const life_talent_def_t c_life_talents[LIFE_TALENT_COUNT] = {')
    for t in TALENTS:
        out.append(f'    {{ {t["id"]}, {t["q"]}, "{t["name"]}", "{t["desc"]}", {t["chr"]}, {t["int"]}, {t["str"]}, {t["mny"]}, {t["joy"]} }},')
    out.append('};')
    out.append('')
    
    out.append('typedef struct {')
    out.append('    uint8_t id;')
    out.append('    uint16_t age;')
    out.append('    const char *title;')
    out.append('    const char *opt_a;')
    out.append('    const char *opt_b;')
    out.append('    const char *res_a;')
    out.append('    const char *res_b;')
    out.append('    int8_t da_int; int8_t da_mny; int8_t da_joy; int8_t da_str; uint8_t da_cyber;')
    out.append('    int8_t db_int; int8_t db_mny; int8_t db_joy; int8_t db_str;')
    out.append('} life_choice_def_t;')
    out.append('')
    out.append(f'#define LIFE_CHOICE_COUNT {len(CHOICES)}')
    out.append('static const life_choice_def_t c_life_choices[LIFE_CHOICE_COUNT] = {')
    for c in CHOICES:
        da = c["da"]
        db = c.get("db", {})
        out.append(f'    {{ {c["id"]}, {c["age"]}, "{c["title"]}", "{c["opt_a"]}", "{c["opt_b"]}", "{c["res_a"]}", "{c["res_b"]}", '
                   f'{da.get("int",0)}, {da.get("mny",0)}, {da.get("joy",0)}, {da.get("str",0)}, {da.get("cyber",0)}, '
                   f'{db.get("int",0)}, {db.get("mny",0)}, {db.get("joy",0)}, {db.get("str",0)} }},')
    out.append('};')
    out.append('')
    
    out.append('typedef struct {')
    out.append('    uint16_t min_age;')
    out.append('    uint16_t max_age;')
    out.append('    uint8_t req_type; // 0:none 1:chr>= 2:int>= 3:str>= 4:mny>= 5:str_low<= 6:joy_low<= 7:cult 8:cyber')
    out.append('    int8_t req_val;')
    out.append('    const char *text;')
    out.append('    int8_t d_chr;')
    out.append('    int8_t d_int;')
    out.append('    int8_t d_str;')
    out.append('    int8_t d_mny;')
    out.append('    int8_t d_joy;')
    out.append('    uint8_t is_die;')
    out.append('} life_event_def_t;')
    out.append('')
    out.append(f'#define LIFE_EVENT_COUNT {len(EVENTS)}')
    out.append('static const life_event_def_t c_life_events[LIFE_EVENT_COUNT] = {')
    req_map = {"": 0, "chr": 1, "int": 2, "str": 3, "mny": 4, "str_low": 5, "joy_low": 6, "cult": 7, "cyber": 8}
    for e in EVENTS:
        rtype = req_map.get(e.get("req_attr", ""), 0)
        rval = e.get("val", 0)
        d = e.get("d", {})
        out.append(f'    {{ {e["min_a"]}, {e["max_a"]}, {rtype}, {rval}, "{e["text"]}", '
                   f'{d.get("chr",0)}, {d.get("int",0)}, {d.get("str",0)}, {d.get("mny",0)}, {d.get("joy",0)}, {d.get("die",0)} }},')
    out.append('};')
    out.append('')

    out.append('typedef struct {')
    out.append('    uint16_t min_score;')
    out.append('    const char *name;')
    out.append('    const char *eval;')
    out.append('} life_title_def_t;')
    out.append('')
    out.append(f'#define LIFE_TITLE_COUNT {len(TITLES)}')
    out.append('static const life_title_def_t c_life_titles[LIFE_TITLE_COUNT] = {')
    for t in TITLES:
        out.append(f'    {{ {t["min_score"]}, "{t["name"]}", "{t["eval"]}" }},')
    out.append('};')
    out.append('')
    out.append('#endif // BUDDY_GAME_LIFE_DATA_H')
    
    with open(r'firmware\main\buddy_game_life_data.h', 'w', encoding='utf-8') as f:
        f.write('\n'.join(out))
    print("Generated firmware/main/buddy_game_life_data.h successfully!")

if __name__ == '__main__':
    chars = collect_all_chinese_chars()
    print(f"Collected {len(chars)} Chinese chars from game text.")
    update_fonts(chars)
    generate_c_data_header()
